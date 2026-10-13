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
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <sstream>

#include <simdutf.h>

namespace
{
    // llstring holds no UTF-32 any more. These keep the tests that were
    // written in codepoints able to say what they mean in bytes: the UTF-8 is
    // built from the codepoints, and every offset is derived from that same
    // build rather than written out by hand. A ZWJ family is 4/3/4/3/4 bytes,
    // so a retyped constant is a wrong answer that still compiles.
    std::string to_utf8(std::u32string_view u32)
    {
        std::string out;
        for (char32_t cp : u32)
        {
            utf8str_append_cp(out, (llwchar)cp);
        }
        return out;
    }

    std::u32string to_u32(std::string_view utf8)
    {
        std::u32string out;
        for (size_t i = 0; i < utf8.size(); )
        {
            const LLCodepointAt at = utf8str_decode_at(utf8, i);
            out.push_back((char32_t)at.cp);
            i = at.next;
        }
        return out;
    }

    // A codepoint index into `u32`, as a byte offset into to_utf8(u32). An
    // index past the end stays past the end by as much, so the walkers' own
    // clamping is what a test of out-of-range input observes.
    size_t cp_to_byte(std::u32string_view u32, size_t index)
    {
        if (index > u32.size())
        {
            return to_utf8(u32).size() + (index - u32.size());
        }
        return to_utf8(u32.substr(0, index)).size();
    }

    // And back. Only character starts are ever handed in, so the walk always
    // lands exactly.
    size_t byte_to_cp(std::u32string_view u32, size_t byte_offset)
    {
        size_t bytes = 0;
        for (size_t i = 0; i < u32.size(); ++i)
        {
            if (bytes >= byte_offset)
            {
                return i;
            }
            bytes += to_utf8(u32.substr(i, 1)).size();
        }
        return u32.size();
    }

    // The tests below were written against wide entry points that llstring no
    // longer has: there is one implementation now, and it works in bytes.
    // These carry the codepoint-indexed cases over it unchanged. Anything that
    // compared the two halves against each other went instead -- against these
    // it would be asking the same implementation twice.
    size_t wstring_step_grapheme_forward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_step_grapheme_forward(to_utf8(w), cp_to_byte(w, pos)));
    }

    size_t wstring_step_grapheme_backward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_step_grapheme_backward(to_utf8(w), cp_to_byte(w, pos)));
    }

    size_t wstring_grapheme_align_backward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_grapheme_align_backward(to_utf8(w), cp_to_byte(w, pos)));
    }

    size_t wstring_grapheme_align_forward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_grapheme_align_forward(to_utf8(w), cp_to_byte(w, pos)));
    }

    size_t wstring_step_word_forward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_step_word_forward(to_utf8(w), cp_to_byte(w, pos)));
    }

    size_t wstring_step_word_backward(std::u32string_view w, size_t pos)
    {
        return byte_to_cp(w, utf8str_step_word_backward(to_utf8(w), cp_to_byte(w, pos)));
    }

    std::pair<size_t, size_t> wstring_word_range_at(std::u32string_view w, size_t pos)
    {
        const auto range = utf8str_word_range_at(to_utf8(w), cp_to_byte(w, pos));
        return { byte_to_cp(w, range.first), byte_to_cp(w, range.second) };
    }

    std::pair<size_t, size_t> wstring_next_word_range(std::u32string_view w, size_t pos)
    {
        const auto range = utf8str_next_word_range(to_utf8(w), cp_to_byte(w, pos));
        return { byte_to_cp(w, range.first), byte_to_cp(w, range.second) };
    }

    void wstring_line_break_opportunities(std::u32string_view w, std::vector<size_t>& out)
    {
        utf8str_line_break_opportunities(to_utf8(w), out);
        for (size_t& at : out)
        {
            at = byte_to_cp(w, at);
        }
    }

    EmojiClusterList wstring_find_emoji_clusters(std::u32string_view w)
    {
        EmojiClusterList runs = utf8str_find_emoji_clusters(to_utf8(w));
        for (auto& run : runs)
        {
            run = { byte_to_cp(w, run.first), byte_to_cp(w, run.second) };
        }
        return runs;
    }

    std::pair<size_t, size_t> wstring_emoji_range_at(std::u32string_view w, size_t pos)
    {
        const auto range = utf8str_emoji_range_at(to_utf8(w), cp_to_byte(w, pos));
        return { byte_to_cp(w, range.first), byte_to_cp(w, range.second) };
    }

    bool wstring_remove_emojis(std::u32string& w)
    {
        std::string utf8 = to_utf8(w);
        const bool removed = utf8str_remove_emojis(utf8);
        w = to_u32(utf8);
        return removed;
    }

    // char32_t has no std::ostream operator<<, so a failed comparison of
    // two std::u32strings could not show them. Compare by value and format
    // diagnostics ourselves.
    void require_same_u32(const std::string& what,
                          const std::u32string& actual,
                          const std::u32string& expected)
    {
        if (actual == expected) return;

        std::ostringstream oss;
        oss << what << ": std::u32string mismatch (actual.size=" << actual.size()
            << " expected.size=" << expected.size() << ")\n  actual: ";
        oss << std::hex << std::uppercase;
        for (llwchar c : actual) oss << "U+" << (U32)c << ' ';
        oss << "\n  expect: ";
        for (llwchar c : expected) oss << "U+" << (U32)c << ' ';
        FAIL(oss.str());
    }

}

namespace
{
    struct llstring_utf_data {};
    // The cases come in categories, which TUT numbered:
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
    //   12x utf8 walkers     (byte-offset segmentation, offsets written out)
    //   14x trim             (Unicode whitespace, and what is not whitespace)
    // Keep this index in sync with the categories used below.

    // ---------------------------------------------------------------
    //                      round-trip coverage
    // ---------------------------------------------------------------

    // Pure ASCII: each of the three conversions llstring has -- UTF-8 to
    // codepoints (utf8str_decode_at, under to_u32), codepoints to UTF-8
    // (utf8str_append_cp, under to_utf8) and UTF-16 to UTF-8 -- keeps every
    // character.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: ASCII converts in every direction", "[LLStringUTF]")
    {
        const std::string ascii = "Hello, World!";
        const std::u32string     wascii (ascii.begin(), ascii.end());
        const llutf16string u16ascii(ascii.begin(), ascii.end());

        require_same_u32("utf8->wstring ASCII", to_u32 (ascii), wascii);
        CHECK(to_utf8 (wascii) == ascii);
        CHECK(utf16str_to_utf8str(u16ascii) == ascii);
    }

    // Latin-1 BMP: 2-byte UTF-8 (é = U+00E9 → C3 A9).
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a two-byte character converts in every direction", "[LLStringUTF]")
    {
        const std::string   utf8 = "H\xC3\xA9llo";
        const std::u32string     w    = { (llwchar)'H', (llwchar)0x00E9, (llwchar)'l', (llwchar)'l', (llwchar)'o' };
        const llutf16string u16  = { (char16_t)'H', (char16_t)0x00E9, (char16_t)'l', (char16_t)'l', (char16_t)'o' };

        require_same_u32("utf8->wstring 2-byte", to_u32 (utf8), w);
        CHECK(to_utf8 (w) == utf8);
        CHECK(utf16str_to_utf8str(u16) == utf8);
    }

    // CJK BMP: 3-byte UTF-8 (日 = U+65E5 → E6 97 A5, 本 = U+672C → E6 9C AC).
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: three-byte CJK converts in every direction", "[LLStringUTF]")
    {
        const std::string   utf8 = "\xE6\x97\xA5\xE6\x9C\xAC";
        const std::u32string     w    = { (llwchar)0x65E5, (llwchar)0x672C };
        const llutf16string u16  = { (char16_t)0x65E5, (char16_t)0x672C };

        require_same_u32("utf8->wstring 3-byte", to_u32 (utf8), w);
        CHECK(to_utf8 (w) == utf8);
        CHECK(utf16str_to_utf8str(u16) == utf8);
    }

    // Astral: 4-byte UTF-8 (U+1F680 rocket → F0 9F 9A 80; UTF-16 surrogate pair D83D DE80).
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: an astral character converts in every direction", "[LLStringUTF]")
    {
        const std::string   utf8 = "\xF0\x9F\x9A\x80";
        const std::u32string     w    = { (llwchar)0x1F680 };
        const llutf16string u16  = { (char16_t)0xD83D, (char16_t)0xDE80 };

        require_same_u32("utf8->wstring astral", to_u32 (utf8), w);
        CHECK(to_utf8 (w) == utf8);
        CHECK(utf16str_to_utf8str(u16) == utf8);
    }

    // Mixed: ASCII + 3-byte BMP + 4-byte astral. Uses explicit string-literal
    // concatenation between astral bytes and the trailing 'B' so \xA5B and
    // \x80B don't eat an extra hex digit.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: mixed one- three- and four-byte text converts in every direction", "[LLStringUTF]")
    {
        const std::string   utf8 = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        const std::u32string     w    = { (llwchar)'A', (llwchar)0x65E5, (llwchar)0x1F680, (llwchar)'B' };
        const llutf16string u16  = { (char16_t)'A', (char16_t)0x65E5, (char16_t)0xD83D, (char16_t)0xDE80, (char16_t)'B' };

        require_same_u32("utf8->wstring mixed", to_u32 (utf8), w);
        CHECK(to_utf8 (w) == utf8);
        CHECK(utf16str_to_utf8str(u16) == utf8);
    }

    // Empty inputs produce empty outputs in every direction.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: empty input converts to empty output in every direction", "[LLStringUTF]")
    {
        CHECK(to_u32 (std::string()).size() == size_t(0));
        CHECK(to_utf8 (std::u32string()) == std::string());
        CHECK(utf16str_to_utf8str(llutf16string()) == std::string());
    }

    // rawstr_to_utf8 repairs through utf8str_sanitize, which hands valid input
    // back as it is.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: rawstr_to_utf8 leaves valid UTF-8 as it is", "[LLStringUTF]")
    {
        const std::string mixed = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        CHECK(rawstr_to_utf8(mixed) == mixed);
    }

    // utf8 <-> u8 reinterpret (not conversion) preserves bytes.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: str_to_u8str and u8str_to_str keep the bytes", "[LLStringUTF]")
    {
        const std::string   bytes = "\xF0\x9F\x9A\x80";
        const std::u8string u8    = str_to_u8str(bytes);
        CHECK(u8.size() == bytes.size());
        CHECK(u8str_to_str(u8) == bytes);
    }

    // ---------------------------------------------------------------
    //                   single-codepoint helpers
    // ---------------------------------------------------------------

    // wchar_to_utf8chars covers 1, 2, 3, 4-byte canonical forms.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wchar_to_utf8chars writes one- to four-byte canonical forms", "[LLStringUTF]")
    {
        char buf[8];
        std::memset(buf, 0xAA, sizeof(buf));

        CHECK(wchar_to_utf8chars(0x41,    buf) == std::ptrdiff_t(1));
        CHECK((U32)(U8)buf[0] == U32(0x41));

        CHECK(wchar_to_utf8chars(0x00E9,  buf) == std::ptrdiff_t(2));
        CHECK((U32)(U8)buf[0] == U32(0xC3));
        CHECK((U32)(U8)buf[1] == U32(0xA9));

        CHECK(wchar_to_utf8chars(0x65E5,  buf) == std::ptrdiff_t(3));
        CHECK((U32)(U8)buf[0] == U32(0xE6));
        CHECK((U32)(U8)buf[1] == U32(0x97));
        CHECK((U32)(U8)buf[2] == U32(0xA5));

        CHECK(wchar_to_utf8chars(0x1F680, buf) == std::ptrdiff_t(4));
        CHECK((U32)(U8)buf[0] == U32(0xF0));
        CHECK((U32)(U8)buf[1] == U32(0x9F));
        CHECK((U32)(U8)buf[2] == U32(0x9A));
        CHECK((U32)(U8)buf[3] == U32(0x80));
    }

    // wchar_to_utf8chars current behaviour: emits 5 and 6-byte legacy
    // encodings for codepoints > U+10FFFF. simdutf rejects these — test
    // pins today's behaviour so the migration change is visible.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wchar_to_utf8chars still writes five- and six-byte forms", "[LLStringUTF]")
    {
        char buf[8];
        std::memset(buf, 0xAA, sizeof(buf));

        CHECK(wchar_to_utf8chars(0x00200000, buf) == std::ptrdiff_t(5));
        CHECK((U32)((U8)buf[0] & 0xF8) == U32(0xF8));

        std::memset(buf, 0xAA, sizeof(buf));
        CHECK(wchar_to_utf8chars(0x04000000, buf) == std::ptrdiff_t(6));
        CHECK((U32)((U8)buf[0] & 0xFC) == U32(0xFC));
    }

    // ---------------------------------------------------------------
    //                      length / count helpers
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wchar_utf8_length counts one to six bytes", "[LLStringUTF]")
    {
        CHECK(wchar_utf8_length(0x00000041) == S32(1));
        CHECK(wchar_utf8_length(0x000000E9) == S32(2));
        CHECK(wchar_utf8_length(0x000065E5) == S32(3));
        CHECK(wchar_utf8_length(0x0001F680) == S32(4));
        CHECK(wchar_utf8_length(0x00200000) == S32(5));
        CHECK(wchar_utf8_length(0x04000000) == S32(6));
    }

#if LL_WINDOWS
    // wide_wstring_length is Windows-only because it assumes std::wstring holds UTF-16.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wide_wstring_length counts a surrogate pair as one codepoint", "[LLStringUTF]")
    {
        std::wstring w;
        w.push_back(L'A');
        w.push_back((wchar_t)0xD83D);
        w.push_back((wchar_t)0xDE80);
        w.push_back(L'B');
        // 4 UTF-16 units = 3 codepoints.
        CHECK(wide_wstring_length(w, 4) == S32(3));
        CHECK(wide_wstring_length(w, 0) == S32(0));
    }
#endif

    // ---------------------------------------------------------------
    //                        truncation helpers
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_truncate cuts ASCII at the length given", "[LLStringUTF]")
    {
        const std::string s = "Hello";
        CHECK(utf8str_truncate(s, 10) == s);
        CHECK(utf8str_truncate(s, 5) == s);
        CHECK(utf8str_truncate(s, 3) == std::string("Hel"));
        CHECK(utf8str_truncate(s, 0) == std::string());
    }

    // Truncation walks back only when max_len lands *inside* a multi-byte
    // sequence (the byte at the cut index is a continuation). "A日B" = 41 E6 97 A5 42.
    //   max_len=2 → cut at 0x97 (cont) → walk back to 1 → "A"
    //   max_len=3 → cut at 0xA5 (cont) → walk back to 1 → "A"
    //   max_len=4 → cut at 0x42 (ASCII 'B') → no walk-back → "A日"
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_truncate backs off only from inside a character", "[LLStringUTF]")
    {
        const std::string s = "A\xE6\x97\xA5" "B";
        CHECK(utf8str_truncate(s, 2) == std::string("A"));
        CHECK(utf8str_truncate(s, 3) == std::string("A"));
        CHECK(utf8str_truncate(s, 4) == std::string("A\xE6\x97\xA5"));
        CHECK(utf8str_truncate(s, 5) == s);
    }

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_substr takes a start and a length", "[LLStringUTF]")
    {
        const std::string s = "ABCDE";
        CHECK(utf8str_substr(s, 1, 3) == std::string("BCD"));
        CHECK(utf8str_substr(s, 3, 2) == std::string("DE"));
        CHECK(utf8str_substr(s, 0, 0) == std::string());
    }

    // utf8str_symbol_truncate returns the first N complete UTF-8 symbols.
    // "A日🚀B" = 1+3+4+1 = 9 bytes, 4 symbols.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_symbol_truncate returns the first N complete UTF-8 symbols", "[LLStringUTF]")
    {
        const std::string s = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        CHECK(utf8str_symbol_truncate(s, 4) == s);
        CHECK(utf8str_symbol_truncate(s, 3) == std::string("A\xE6\x97\xA5\xF0\x9F\x9A\x80"));
        CHECK(utf8str_symbol_truncate(s, 2) == std::string("A\xE6\x97\xA5"));
        CHECK(utf8str_symbol_truncate(s, 1) == std::string("A"));
        CHECK(utf8str_symbol_truncate(s, 0) == std::string());
    }

    // ---------------------------------------------------------------
    //              invalid-input pinning (breaking-change flags)
    // ---------------------------------------------------------------

    // Lone continuation byte (0x80) and invalid leader (0xFF) each map to
    // one LL_UNKNOWN_CHAR.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a lone continuation or invalid lead byte is one bad character", "[LLStringUTF]")
    {
        for (const std::string& bad : { std::string("\x80"), std::string("\xFF") })
        {
            REQUIRE_FALSE(utf8str_is_valid(bad));

            // The decoder gives one replacement character per bad byte, so a
            // walk driven off it always advances.
            const std::u32string w = to_u32(bad);
            REQUIRE(w.size() == size_t(1));
            REQUIRE((U32)w[0] == U32(0xFFFD));

            // Repair substitutes LL_UNKNOWN_CHAR, and the result is valid.
            const std::string fixed = utf8str_sanitize(bad);
            REQUIRE(fixed == std::string(1, LL_UNKNOWN_CHAR));
            REQUIRE(utf8str_is_valid(fixed));
        }
    }

    // Overlong 2-byte NUL "\xC0\x80" — simdutf flags both bytes as invalid
    // (0xC0 is never a legal 2-byte leader; 0x80 is a lone continuation), so
    // we get one '?' per rejected byte. The previous hand-rolled decoder
    // produced a single '?' per malformed sequence; this behavioural change
    // is intentional for the migration.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: an overlong two-byte NUL is two bad bytes", "[LLStringUTF]")
    {
        const std::string overlong("\xC0\x80");
        CHECK_FALSE(utf8str_is_valid(overlong));

        const std::u32string w = to_u32(overlong);
        REQUIRE(w.size() == size_t(2));
        CHECK((U32)w[0] == U32(0xFFFD));
        CHECK((U32)w[1] == U32(0xFFFD));

        CHECK(utf8str_sanitize(overlong) == std::string(2, LL_UNKNOWN_CHAR));
    }

    // Truncated 3-byte sequence "A\xE6\x97" — simdutf rejects both 0xE6 (leader
    // without a full body) and 0x97 (lone continuation), so the result is
    // "A??" (A + two replacement chars). Previous hand-rolled decoder
    // produced "A?".
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a truncated three-byte sequence is two bad bytes", "[LLStringUTF]")
    {
        const std::string truncated("A\xE6\x97");
        CHECK_FALSE(utf8str_is_valid(truncated));

        const std::u32string w = to_u32(truncated);
        REQUIRE(w.size() == size_t(3));
        CHECK((U32)w[0] == U32('A'));
        CHECK((U32)w[1] == U32(0xFFFD));
        CHECK((U32)w[2] == U32(0xFFFD));

        CHECK(utf8str_sanitize(truncated) == std::string("A??"));
    }

    // Legacy 5-byte UTF-8 encoding (U+00200000 → F8 88 80 80 80) is no longer
    // accepted — RFC-3629 compliance. Each byte is flagged individually, so
    // the input becomes 5 replacement chars instead of 1 wchar with the legacy
    // codepoint.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a legacy five-byte sequence is five bad bytes", "[LLStringUTF]")
    {
        const std::string legacy("\xF8\x88\x80\x80\x80");
        CHECK_FALSE(utf8str_is_valid(legacy));

        const std::u32string w = to_u32(legacy);
        CHECK(w.size() == size_t(5));
        for (size_t i = 0; i < w.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE((U32)w[i] == U32(0xFFFD));
        }

        CHECK(utf8str_sanitize(legacy) == std::string(5, LL_UNKNOWN_CHAR));
    }

    // UTF-16 lone high surrogate followed by a non-surrogate: simdutf rejects
    // the lone 0xD83D (one '?') and converts the following 0x0041 normally.
    // Previous hand-rolled decoder greedily consumed both units as one garbage
    // codepoint.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a lone UTF-16 high surrogate becomes one unknown character", "[LLStringUTF]")
    {
        const llutf16string bad = { (char16_t)0xD83D, (char16_t)0x0041 };
        const std::string out = utf16str_to_utf8str(bad);
        REQUIRE(out.size() == size_t(2));
        CHECK((U32)(U8)out[0] == U32((U8)LL_UNKNOWN_CHAR));
        CHECK((U32)(U8)out[1] == U32('A'));
    }

    // ---------------------------------------------------------------
    //                    round-trip helpers (smoke)
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_trim and utf8str_tolower keep non-ASCII text", "[LLStringUTF]")
    {
        // Both work on the UTF-8 itself -- trim by the offsets of its
        // whitespace, tolower through LLStringUtil::toLower. Covered here so a
        // regression that breaks a character surfaces before we diagnose it
        // through callers.
        const std::string mixed = "H\xC3\xA9LLO \xE6\x97\xA5";
        CHECK(utf8str_trim(mixed) == mixed);
        CHECK(utf8str_tolower(mixed) == std::string("h\xC3\xA9llo \xE6\x97\xA5"));
    }

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_compare_insensitive orders regardless of case", "[LLStringUTF]")
    {
        CHECK(utf8str_compare_insensitive("Hello", "hello") == S32(0));
        CHECK(utf8str_compare_insensitive("abc", "abd") < 0);
        CHECK(utf8str_compare_insensitive("abd", "abc") > 0);
    }

    // ---------------------------------------------------------------
    //                         emoji helpers
    // ---------------------------------------------------------------

    // LLStringOps::isEmoji is Unicode's Emoji_Presentation: does this render
    // in colour on its own. Symbols that only become emoji when a VS-16 asks
    // them to (©, ®, ❤) are not, and neither is a codepoint that merely sits
    // in an emoji block.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: LLStringOps::isEmoji is Unicode's Emoji_Presentation", "[LLStringUTF]")
    {
        UNSCOPED_INFO("isEmoji rocket");
        CHECK(LLStringOps::isEmoji((llwchar)0x1F680));
        // Both mahjong tiles, one of which Unicode calls an emoji and one of
        // which it does not. A block-shaped range test cannot tell them apart.
        UNSCOPED_INFO("isEmoji mahjong red dragon");
        CHECK(LLStringOps::isEmoji((llwchar)0x1F004));
        UNSCOPED_INFO("not isEmoji mahjong east wind");
        CHECK_FALSE(LLStringOps::isEmoji((llwchar)0x1F000));
        // A BMP emoji, which the old astral-only range could never see.
        UNSCOPED_INFO("isEmoji watch");
        CHECK(LLStringOps::isEmoji((llwchar)0x231A));
        CHECK_FALSE(LLStringOps::isEmoji((llwchar)'A'));
        UNSCOPED_INFO("not isEmoji ©");
        CHECK_FALSE(LLStringOps::isEmoji((llwchar)0x00A9));
        UNSCOPED_INFO("not isEmoji ❤");
        CHECK_FALSE(LLStringOps::isEmoji((llwchar)0x2764));
        UNSCOPED_INFO("not isEmoji CJK-Ext");
        CHECK_FALSE(LLStringOps::isEmoji((llwchar)0x20000));
    }

    // wstring_remove_emojis must strip consecutive emojis without skipping
    // the code point that follows them.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wstring_remove_emojis strips consecutive emoji and keeps what follows", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'H', (llwchar)0x1F680, (llwchar)0x1F681, (llwchar)'i' };
        const std::u32string expected = { (llwchar)'H', (llwchar)'i' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("consecutive emojis stripped", ws, expected);

        std::u32string none = { (llwchar)'H', (llwchar)'i' };
        CHECK_FALSE(wstring_remove_emojis(none));
        require_same_u32("unchanged when no emojis", none, expected);
    }

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_remove_emojis strips whole clusters from UTF-8", "[LLStringUTF]")
    {
        std::string s = "Hi\xF0\x9F\x9A\x80" "!";
        CHECK(utf8str_remove_emojis(s));
        CHECK(s == std::string("Hi!"));

        std::string clean = "Hello";
        CHECK_FALSE(utf8str_remove_emojis(clean));
        CHECK(clean == std::string("Hello"));

        // Heart-on-fire ❤️‍🔥 goes as one cluster, bytes and all.
        // UTF-8 byte sequence: U+2764 (E2 9D A4) U+FE0F (EF B8 8F)
        // U+200D (E2 80 8D) U+1F525 (F0 9F 94 A5).
        std::string fire = "A" "\xE2\x9D\xA4" "\xEF\xB8\x8F" "\xE2\x80\x8D" "\xF0\x9F\x94\xA5" "B";
        CHECK(utf8str_remove_emojis(fire));
        CHECK(fire == std::string("AB"));

        // Subdivision flag 🏴󠁧󠁢󠁥󠁮󠁧󠁿 — base + 5 tag chars + CANCEL TAG, all
        // astral. UTF-8 bytes: U+1F3F4 (F0 9F 8F B4) followed by 6 four-byte
        // tag-char sequences.
        std::string flag = "[" "\xF0\x9F\x8F\xB4"
                                "\xF3\xA0\x81\xA7" "\xF3\xA0\x81\xA2" "\xF3\xA0\x81\xB3"
                                "\xF3\xA0\x81\xA3" "\xF3\xA0\x81\xB4" "\xF3\xA0\x81\xBF"
                            "]";
        CHECK(utf8str_remove_emojis(flag));
        CHECK(flag == std::string("[]"));
    }

    // ZWJ family (U+1F468 U+200D U+1F469 U+200D U+1F467) must strip as a
    // single unit — the old implementation left the two ZWJ code points
    // behind, producing visible tofu.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a ZWJ family strips as one unit", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'H',
                         (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467,
                         (llwchar)'i' };
        const std::u32string expected = { (llwchar)'H', (llwchar)'i' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("ZWJ family stripped", ws, expected);
    }

    // Skin-tone modifier (U+1F3FB..FF) must strip together with its base emoji.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a skin-tone modifier strips with its base emoji", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'M',
                         (llwchar)0x1F468, (llwchar)0x1F3FB,
                         (llwchar)'X' };
        const std::u32string expected = { (llwchar)'M', (llwchar)'X' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("skin tone stripped", ws, expected);
    }

    // Regional indicator flag (🇺🇸 = U+1F1FA U+1F1F8). Both code points are in
    // the astral emoji range so both strip — the output must not leave a
    // dangling half-flag indicator.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a flag strips both its regional indicators", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("flag stripped", ws, expected);
    }

    // Trailing VS16 (U+FE0F) after an astral emoji must be consumed together
    // with the base glyph.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a trailing VS16 strips with its astral base", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0F, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("VS16 stripped with base", ws, expected);
    }

    // Bare ZWJ or VS16 (no adjacent emoji) are not emoji themselves and must
    // be left intact — critical because ZWJ is used in Arabic/Indic shaping
    // outside any emoji context.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a bare ZWJ is not an emoji and stays", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        const std::u32string expected = ws;
        CHECK_FALSE(wstring_remove_emojis(ws));
        require_same_u32("bare ZWJ preserved", ws, expected);
    }

    // Broad-contract rewrite: wstring_remove_emojis now drives off the
    // cluster walker, so anything HarfBuzz would itemize as one emoji glyph
    // is removed — including BMP-base sequences like the heart + VS-16
    // pair (which the walker treats as a 2-codepoint cluster). Earlier
    // builds left this pair intact under a narrower isEmoji-only contract;
    // the change unifies the strip behavior with cluster-aware tooltip and
    // cursor logic so they can't disagree on cluster bounds.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a heart with VS16 strips but a bare heart stays", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x2764, (llwchar)0xFE0F };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("BMP heart+VS16 cleared", ws, std::u32string());

        // Bare BMP heart (no VS-16) is not a cluster — single-codepoint
        // pictograph, shaped via the 1:1 path. Outside isEmoji's narrow
        // range too. Must pass through.
        std::u32string bare_heart = { (llwchar)0x2764 };
        const std::u32string bare_expected = bare_heart;
        CHECK_FALSE(wstring_remove_emojis(bare_heart));
        require_same_u32("bare heart preserved", bare_heart, bare_expected);
    }

    // ---------------------------------------------------------------
    //          7x emoji broad-strip (cluster-driven)
    // ---------------------------------------------------------------

    // Heart-on-fire ❤️‍🔥 (U+2764 U+FE0F U+200D U+1F525): full ZWJ sequence
    // with a BMP base. Pre-rewrite the walker would write through the
    // heart/VS-16/ZWJ then strip the lone fire, leaving `❤️‍` (heart +
    // VS-16 + dangling ZWJ) on screen. Cluster-driven removal strips
    // the whole sequence as one unit.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: heart-on-fire strips whole though its base is in the BMP", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'A',
                         (llwchar)0x2764, (llwchar)0xFE0F,
                         (llwchar)0x200D, (llwchar)0x1F525,
                         (llwchar)'B' };
        const std::u32string expected = { (llwchar)'A', (llwchar)'B' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("heart-on-fire cleared", ws, expected);
    }

    // Keycap `1️⃣` (`'1' + VS-16 + U+20E3`): no codepoint inside the cluster
    // is isEmoji-true (digit is ASCII, VS-16 / keycap combiner are BMP), so
    // the old narrow walker left the whole thing intact. The cluster walker
    // identifies it as a single 3-codepoint cluster, and the broad contract
    // strips it.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: keycaps strip as clusters and a bare digit stays", "[LLStringUTF]")
    {
        std::u32string digit_kc = { (llwchar)'1', (llwchar)0xFE0F, (llwchar)0x20E3, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        CHECK(wstring_remove_emojis(digit_kc));
        require_same_u32("digit keycap cleared", digit_kc, expected);

        std::u32string hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        CHECK(wstring_remove_emojis(hash_kc));
        require_same_u32("hash keycap cleared", hash_kc, std::u32string());

        std::u32string star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
        CHECK(wstring_remove_emojis(star_kc));
        require_same_u32("star keycap cleared", star_kc, std::u32string());

        // Bare digit (no VS-16 + combiner) must pass through. The cluster
        // walker rejects it as a starter; nothing to strip.
        std::u32string bare_digit = { (llwchar)'9' };
        const std::u32string bare_expected = bare_digit;
        CHECK_FALSE(wstring_remove_emojis(bare_digit));
        require_same_u32("bare digit preserved", bare_digit, bare_expected);
    }

    // Subdivision flag 🏴󠁧󠁢󠁥󠁮󠁧󠁿 (U+1F3F4 + tag bytes + U+E007F CANCEL TAG).
    // Pre-rewrite the walker stripped the astral 🏴 base via isEmoji but
    // left tag chars and the U+E007F terminator behind as garbage code
    // points. Cluster-driven removal sweeps the whole sequence.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a subdivision flag strips with its tag characters", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'<',
                         (llwchar)0x1F3F4,
                         (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                         (llwchar)0xE0063, (llwchar)0xE0074,
                         (llwchar)0xE007F,
                         (llwchar)'>' };
        const std::u32string expected = { (llwchar)'<', (llwchar)'>' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("subdivision flag cleared", ws, expected);
    }

    // Astral emoji + VS-15 (U+FE0E text-presentation selector) — VS-15 is
    // an extender for the cluster walker, so it's part of the cluster and
    // gets stripped together with its base. Pre-rewrite VS-15 was not in
    // is_emoji_sequence_extender's narrow set and would have been left as
    // an orphan after the base was stripped.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a VS15 after an astral emoji strips with it", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0E, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("VS-15 cluster cleared", ws, expected);
    }

    // Two flags back-to-back 🇺🇸🇯🇵 — distinct clusters [0,2) and [2,4).
    // Both must strip cleanly; no leftover RI codepoints.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: two flags back to back both strip", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                         (llwchar)0x1F1EF, (llwchar)0x1F1F5,
                         (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("two flags cleared", ws, expected);
    }

    // ZWSP (U+200B) is not an emoji extender — it must pass through even
    // when adjacent to an astral emoji. Pre-rewrite this happened to work
    // (ZWSP isn't in the extender set), but pin it now that the path uses
    // the cluster walker, which also treats ZWSP as inert.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a ZWSP next to an emoji is not stripped", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0x200B, (llwchar)'X' };
        const std::u32string expected = { (llwchar)0x200B, (llwchar)'X' };
        CHECK(wstring_remove_emojis(ws));
        require_same_u32("rocket stripped, ZWSP kept", ws, expected);
    }

    // ---------------------------------------------------------------
    //                  11x utf8str helper smoke pins
    // ---------------------------------------------------------------

    // ---------------------------------------------------------------
    //                     byte-level helpers
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: is_char_hex accepts hex digits only", "[LLStringUTF]")
    {
        for (char c : std::string("0123456789abcdefABCDEF"))
        {
            CAPTURE(c);
            REQUIRE(is_char_hex(c));
        }
        CHECK_FALSE(is_char_hex('g'));
        CHECK_FALSE(is_char_hex('Z'));
        CHECK_FALSE(is_char_hex(' '));
    }

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: hex_as_nybble reads either case and gives 0 otherwise", "[LLStringUTF]")
    {
        CHECK((U32)hex_as_nybble('0') == U32(0));
        CHECK((U32)hex_as_nybble('9') == U32(9));
        CHECK((U32)hex_as_nybble('a') == U32(10));
        CHECK((U32)hex_as_nybble('f') == U32(15));
        CHECK((U32)hex_as_nybble('A') == U32(10));
        CHECK((U32)hex_as_nybble('F') == U32(15));
        // Non-hex inputs fall through to 0 (undefined-but-documented behavior).
        CHECK((U32)hex_as_nybble('g') == U32(0));
    }

    // ---------------------------------------------------------------
    //                 shaping-run detector
    // ---------------------------------------------------------------

    // Inputs that never need shaping — empty, ASCII, CJK, a single isolated
    // emoji, and two adjacent but unrelated emoji all render correctly via
    // the 1:1 codepoint path, so the detector must return no runs.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: text that needs no shaping has no emoji clusters", "[LLStringUTF]")
    {
        CHECK(wstring_find_emoji_clusters(std::u32string()).size() == size_t(0));
        std::u32string ascii = { (llwchar)'H', (llwchar)'i', (llwchar)'!' };
        CHECK(wstring_find_emoji_clusters(ascii).size() == size_t(0));
        std::u32string cjk   = { (llwchar)0x65E5, (llwchar)0x672C };
        CHECK(wstring_find_emoji_clusters(cjk).size() == size_t(0));
        std::u32string lone  = { (llwchar)0x1F680 };
        CHECK(wstring_find_emoji_clusters(lone).size() == size_t(0));
        std::u32string pair  = { (llwchar)0x1F680, (llwchar)0x1F681 };
        CHECK(wstring_find_emoji_clusters(pair).size() == size_t(0));
    }

    // ZWJ family 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 — must emerge
    // as a single 5-codepoint run.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a ZWJ family is one five-codepoint cluster", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467 };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].first == size_t(0));
        CHECK(runs[0].second == size_t(5));
    }

    // Skin-tone modifier (U+1F3FB..FF) combines with the preceding emoji.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a skin-tone modifier joins the emoji before it", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F468, (llwchar)0x1F3FB };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].second == size_t(2));
    }

    // Trailing VS16 (U+FE0F) forces emoji presentation and must be part of
    // the same shaped run as its base.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a trailing VS16 joins its base's cluster", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].second == size_t(2));
    }

    // Regional indicator pair — 🇺🇸 = U+1F1FA U+1F1F8 — becomes a flag glyph
    // only under shaping. Two consecutive RIs form exactly one pair.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: regional indicators pair into flag clusters", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].second == size_t(2));
        // Four RIs in a row form two separate flags.
        std::u32string four = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                           (llwchar)0x1F1EB, (llwchar)0x1F1F7 };
        auto more = wstring_find_emoji_clusters(four);
        REQUIRE(more.size() == size_t(2));
        CHECK(more[0].second == size_t(2));
        CHECK(more[1].first == size_t(2));
        CHECK(more[1].second == size_t(4));
    }

    // Keycap: digit/#/* + U+FE0F + U+20E3 is a shaped 3-codepoint sequence.
    // shapeRun itemises this into per-face sub-runs (the ASCII digit on
    // the text font, the combining mark on the emoji font), so the
    // cluster is still detected here for cursor-snapping purposes.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a keycap is a three-codepoint cluster", "[LLStringUTF]")
    {
        std::u32string digit_kc = { (llwchar)'9', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto runs = wstring_find_emoji_clusters(digit_kc);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].second == size_t(3));
        std::u32string hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        CHECK(wstring_find_emoji_clusters(hash_kc).size() == size_t(1));
        std::u32string bare = { (llwchar)'5' };
        CHECK(wstring_find_emoji_clusters(bare).size() == size_t(0));
    }

    // Subdivision flag — base 🏴 (U+1F3F4) + tag characters (U+E0020..U+E007F)
    // terminated by U+E007F. All tag bytes must be absorbed into the run.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a subdivision flag's cluster takes in its tag characters", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x1F3F4,
                         (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                         (llwchar)0xE0063, (llwchar)0xE0074,
                         (llwchar)0xE007F };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].first == size_t(0));
        CHECK(runs[0].second == size_t(7));
    }

    // Mixed: ASCII + flag + ASCII + ZWJ family + ASCII. Two disjoint runs
    // with correct bounds; surrounding ASCII is untouched.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a flag and a ZWJ family among ASCII are two clusters", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)'H',
                         (llwchar)0x1F1FA, (llwchar)0x1F1F8,       // flag @ [1,3)
                         (llwchar)' ',
                         (llwchar)0x1F468, (llwchar)0x200D,        // family @ [4,9)
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467,
                         (llwchar)'!' };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(2));
        CHECK(runs[0].first == size_t(1));
        CHECK(runs[0].second == size_t(3));
        CHECK(runs[1].first == size_t(4));
        CHECK(runs[1].second == size_t(9));
    }

    // Bare ZWJ/VS16 surrounded by non-emoji (as used in Arabic/Indic shaping
    // outside any emoji context) must NOT produce a run. There is no emoji
    // face to shape with and the caller should keep the 1:1 path.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: bare extenders and malformed sequences cluster as pinned", "[LLStringUTF]")
    {
        std::u32string zwj  = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        CHECK(wstring_find_emoji_clusters(zwj).size() == size_t(0));
        std::u32string vs16 = { (llwchar)'a', (llwchar)0xFE0F, (llwchar)'b' };
        CHECK(wstring_find_emoji_clusters(vs16).size() == size_t(0));

        // Malformed inputs: leading/trailing extenders, lone modifiers, and
        // singletons that need a partner. Pin current behavior so future
        // refactors don't silently change cluster boundaries on broken input.
        std::u32string lead_zwj  = { (llwchar)0x200D, (llwchar)0x1F468 };
        CHECK(wstring_find_emoji_clusters(lead_zwj).size() == size_t(0));
        std::u32string trail_zwj = { (llwchar)0x1F468, (llwchar)0x200D };
        // Trailing ZWJ: is_shaping_starter accepts the base because the
        // next codepoint is a ZWJ, but advance_shaping_run hits the
        // orphan-ZWJ break and stops at r=1. The cluster walker discards
        // the resulting length-1 "run" so the result matches the doc
        // contract (isolated single-codepoint emoji are intentionally
        // absent from the cluster list).
        CHECK(wstring_find_emoji_clusters(trail_zwj).size() == size_t(0));
        // Same shape but with VS-16 instead of ZWJ: 'is_shaping_starter'
        // accepts because VS-16 is a valid extender, advance consumes it,
        // run length is 2 → real 2-codepoint cluster. Confirm we didn't
        // over-trim and accidentally drop legitimate 2-codepoint clusters.
        std::u32string base_vs16 = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto vs_runs = wstring_find_emoji_clusters(base_vs16);
        REQUIRE(vs_runs.size() == size_t(1));
        CHECK(vs_runs[0].first == size_t(0));
        CHECK(vs_runs[0].second == size_t(2));
        std::u32string lone_skin = { (llwchar)0x1F3FB };
        CHECK(wstring_find_emoji_clusters(lone_skin).size() == size_t(0));
        std::u32string lone_ri = { (llwchar)0x1F1FA };
        CHECK(wstring_find_emoji_clusters(lone_ri).size() == size_t(0));

        // Just either side of the BMP window the old range test had. Neither
        // is pictographic.
        std::u32string just_below_astral = { (llwchar)0x1FFF };
        CHECK(wstring_find_emoji_clusters(just_below_astral).size() == size_t(0));
        std::u32string just_above_bmp    = { (llwchar)0x3300 };
        CHECK(wstring_find_emoji_clusters(just_above_bmp).size() == size_t(0));

        // Back-to-back ZWJ families with no separator. Pin current behavior:
        // each MAN-ZWJ-WOMAN sub-sequence registers as its own cluster, so we
        // expect two runs of length 3 each.
        std::u32string two_fam = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                              (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469 };
        auto two_fam_runs = wstring_find_emoji_clusters(two_fam);
        REQUIRE(two_fam_runs.size() == size_t(2));
        CHECK(two_fam_runs[0].first == size_t(0));
        CHECK(two_fam_runs[0].second == size_t(3));
        CHECK(two_fam_runs[1].first == size_t(3));
        CHECK(two_fam_runs[1].second == size_t(6));

        // ZWJ followed by a non-pictograph: starter accepts the base
        // (next is ZWJ), advance_shaping_run reaches the ZWJ, sees the
        // non-pictograph 'A' afterwards, breaks orphan-ZWJ. Length-1 run
        // gets discarded by the cluster walker. No cluster emitted.
        std::u32string zwj_plus_nonemoji = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)'A' };
        CHECK(wstring_find_emoji_clusters(zwj_plus_nonemoji).size() == size_t(0));

        // Double ZWJ (ZWJ + ZWJ between bases): advance hits the first
        // ZWJ, looks at the next codepoint, finds another ZWJ which is
        // not a pictograph base. Orphan-ZWJ break. Length 1, discarded.
        std::u32string double_zwj = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x200D, (llwchar)0x1F469 };
        CHECK(wstring_find_emoji_clusters(double_zwj).size() == size_t(0));

        // Truncated subdivision flag — base 🏴 + a couple of tag bytes,
        // no U+E007F terminator. Pin: walker greedily eats every tag
        // codepoint to end-of-string. Real implementation behavior.
        std::u32string trunc_tag = { (llwchar)0x1F3F4, (llwchar)0xE0067, (llwchar)0xE0062 };
        auto trunc_runs = wstring_find_emoji_clusters(trunc_tag);
        REQUIRE(trunc_runs.size() == size_t(1));
        CHECK(trunc_runs[0].first == size_t(0));
        CHECK(trunc_runs[0].second == size_t(3));

        // Three RIs — first two form a flag at [0,2), third RI alone is
        // not a starter (no partner). Pin: one cluster, length 2.
        std::u32string three_ri = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1F8 };
        auto three_ri_runs = wstring_find_emoji_clusters(three_ri);
        REQUIRE(three_ri_runs.size() == size_t(1));
        CHECK(three_ri_runs[0].first == size_t(0));
        CHECK(three_ri_runs[0].second == size_t(2));

        // Star keycap '*' + VS-16 + U+20E3. Pin parity with digit/hash.
        std::u32string star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto star_runs = wstring_find_emoji_clusters(star_kc);
        REQUIRE(star_runs.size() == size_t(1));
        CHECK(star_runs[0].second == size_t(3));
    }

    // BMP pictograph + VS16 + ZWJ + astral emoji (❤️‍🔥 = U+2764 U+FE0F
    // U+200D U+1F525). The base ❤ is not one LLStringOps::isEmoji accepts,
    // but the shaping detector must still pick up the whole sequence
    // so HarfBuzz gets to compose the "heart on fire" glyph instead of
    // leaving ❤ and 🔥 as disjoint codepoints.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: heart-on-fire is one cluster though its base is in the BMP", "[LLStringUTF]")
    {
        std::u32string ws = { (llwchar)0x2764, (llwchar)0xFE0F,
                         (llwchar)0x200D, (llwchar)0x1F525 };
        auto runs = wstring_find_emoji_clusters(ws);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].first == size_t(0));
        CHECK(runs[0].second == size_t(4));
    }

    // ---------------------------------------------------------------
    //                 grapheme-step cursor walker
    // ---------------------------------------------------------------

    // Forward step must move exactly one codepoint through ASCII and skip
    // past entire emoji clusters in one move so the caret never lands mid-
    // ZWJ-family.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: stepping forward crosses one character or one whole cluster", "[LLStringUTF]")
    {
        // Pure ASCII — one codepoint per step, clamped at size.
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        CHECK(wstring_step_grapheme_forward(ascii, 0) == size_t(1));
        CHECK(wstring_step_grapheme_forward(ascii, 2) == size_t(3));
        CHECK(wstring_step_grapheme_forward(ascii, 3) == size_t(3));

        // ZWJ family in the middle: H, 👨, ZWJ, 👩, ZWJ, 👧, i
        //                           0  1    2    3    4    5    6
        std::u32string fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        CHECK(wstring_step_grapheme_forward(fam, 0) == size_t(1));
        UNSCOPED_INFO("fam start jumps past cluster");
        CHECK(wstring_step_grapheme_forward(fam, 1) == size_t(6));
        UNSCOPED_INFO("from mid-cluster snaps past");
        CHECK(wstring_step_grapheme_forward(fam, 3) == size_t(6));
        CHECK(wstring_step_grapheme_forward(fam, 6) == size_t(7));

        // Regional indicator flag: US = U+1F1FA U+1F1F8. One step covers both.
        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        CHECK(wstring_step_grapheme_forward(flag, 0) == size_t(2));
    }

    // Backward step is the mirror: one codepoint through ASCII, snap to the
    // start of any emoji cluster we'd otherwise land inside.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: stepping back crosses one character or one whole cluster", "[LLStringUTF]")
    {
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        CHECK(wstring_step_grapheme_backward(ascii, 3) == size_t(2));
        CHECK(wstring_step_grapheme_backward(ascii, 1) == size_t(0));
        CHECK(wstring_step_grapheme_backward(ascii, 0) == size_t(0));

        std::u32string fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        UNSCOPED_INFO("end of string -> 'i' start");
        CHECK(wstring_step_grapheme_backward(fam, 7) == size_t(6));
        UNSCOPED_INFO("'i' start snaps back to cluster start");
        CHECK(wstring_step_grapheme_backward(fam, 6) == size_t(1));
        UNSCOPED_INFO("mid-cluster snaps to cluster start");
        CHECK(wstring_step_grapheme_backward(fam, 4) == size_t(1));
        UNSCOPED_INFO("cluster start -> 'H'");
        CHECK(wstring_step_grapheme_backward(fam, 1) == size_t(0));

        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        UNSCOPED_INFO("the whole flag is one cluster");
        CHECK(wstring_step_grapheme_backward(flag, 2) == size_t(0));
    }

    // ---------------------------------------------------------------
    // LLStringOps::isPictographBase: Extended_Pictographic plus the regional
    // indicators, so heart-on-fire (U+2764 + ZWJ + U+1F525) registers as an
    // emoji-cluster start and flags still pair up.
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: LLStringOps::isPictographBase is Extended_Pictographic and flags", "[LLStringUTF]")
    {
        // Astral emoji: both predicates accept.
        UNSCOPED_INFO("isPictographBase rocket");
        CHECK(LLStringOps::isPictographBase((llwchar)0x1F680));
        UNSCOPED_INFO("isPictographBase fire");
        CHECK(LLStringOps::isPictographBase((llwchar)0x1F525));
        // BMP pictographs that isEmoji rejects but isPictographBase accepts.
        UNSCOPED_INFO("isPictographBase heart");
        CHECK(LLStringOps::isPictographBase((llwchar)0x2764));
        UNSCOPED_INFO("isPictographBase copyright");
        CHECK(LLStringOps::isPictographBase((llwchar)0x00A9));
        UNSCOPED_INFO("isPictographBase registered");
        CHECK(LLStringOps::isPictographBase((llwchar)0x00AE));
        // Plain ASCII / digits never qualify.
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)'A'));
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)'0'));
        // ZWJ and VS-16 are extenders, not bases.
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x200D));
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0xFE0F));
        // Just outside the windows the old range test had: a CJK ideograph
        // past the astral one, and either side of the BMP one.
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x20000));
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x1FFF));
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x3300));

        // A flag is two regional indicators and nothing else. Unicode does
        // not call them pictographic, so they are named here separately;
        // reading Extended_Pictographic alone would stop flags clustering.
        CHECK(LLStringOps::isPictographBase((llwchar)0x1F1FA));

        // The old range ran from U+2000 to U+3300, which swept up punctuation,
        // maths, and every kana and Hangul jamo along the way.
        UNSCOPED_INFO("not isPictographBase EN QUAD");
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x2000));
        UNSCOPED_INFO("not isPictographBase summation");
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x2211));
        UNSCOPED_INFO("not isPictographBase hiragana");
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x3042));
        UNSCOPED_INFO("not isPictographBase katakana");
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x30A2));
        UNSCOPED_INFO("not isPictographBase Hangul jamo");
        CHECK_FALSE(LLStringOps::isPictographBase((llwchar)0x3131));
    }

    // ---------------------------------------------------------------
    // wstring_emoji_range_at: cluster bounds at a hit-test position.
    // Inclusive of cluster boundaries (unlike grapheme_align_*) and
    // synthesises a [pos, pos+1) range for single-codepoint emoji that
    // wstring_find_emoji_clusters skips.
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wstring_emoji_range_at gives the cluster or pictograph at a position", "[LLStringUTF]")
    {
        // ZWJ family "H 👨 ZWJ 👩 ZWJ 👧 i" — cluster spans [1, 6).
        std::u32string fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        auto on_lead   = wstring_emoji_range_at(fam, 1);
        auto inside    = wstring_emoji_range_at(fam, 3);
        auto on_end    = wstring_emoji_range_at(fam, 6);
        CHECK(on_lead.first == size_t(1));
        CHECK(on_lead.second == size_t(6));
        CHECK(inside.first == size_t(1));
        CHECK(inside.second == size_t(6));
        // pos == run.second is past the cluster — falls through to the
        // codepoint there ('i', non-emoji), so empty range.
        CHECK(on_end.first == on_end.second);

        // Plain ASCII: empty range everywhere.
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        auto a0 = wstring_emoji_range_at(ascii, 0);
        auto a2 = wstring_emoji_range_at(ascii, 2);
        CHECK(a0.first == a0.second);
        CHECK(a2.first == a2.second);

        // Single-codepoint emoji 😀 (U+1F600): not in cluster list, but
        // the helper synthesises [pos, pos+1).
        std::u32string lone = { (llwchar)0x1F600 };
        auto le = wstring_emoji_range_at(lone, 0);
        CHECK(le.first == size_t(0));
        CHECK(le.second == size_t(1));

        // Out-of-bounds and exact-end positions are empty.
        auto at_end   = wstring_emoji_range_at(ascii, 3);
        auto past_end = wstring_emoji_range_at(ascii, 99);
        CHECK(at_end.first == at_end.second);
        CHECK(past_end.first == past_end.second);

        // Empty wstring: empty range.
        auto empty = wstring_emoji_range_at(std::u32string(), 0);
        CHECK(empty.first == empty.second);

        // Flag pair 🇺🇸 — one cluster spanning [0, 2).
        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto f0 = wstring_emoji_range_at(flag, 0);
        auto f1 = wstring_emoji_range_at(flag, 1);
        CHECK(f0.first == size_t(0));
        CHECK(f0.second == size_t(2));
        CHECK(f1.first == size_t(0));
        CHECK(f1.second == size_t(2));

        // BMP pictographs — the orthodox cross bug. isPictographBase accepts
        // them, being Extended_Pictographic, as it does © U+00A9 and ®
        // U+00AE; isEmoji rejects them. Tooltip lookup must still find them.
        auto require_single_range = [](llwchar cp, const char* tag) {
            std::u32string ws { cp };
            auto r = wstring_emoji_range_at(ws, 0);
            INFO(tag);
            REQUIRE(r.first == size_t(0));
            REQUIRE(r.second == size_t(1));
        };
        require_single_range((llwchar)0x2626, "orthodox cross");
        require_single_range((llwchar)0x00A9, "copyright");
        require_single_range((llwchar)0x00AE, "registered");
        require_single_range((llwchar)0x2764, "heart");
        require_single_range((llwchar)0x2693, "anchor");

        // Bare extenders, and codepoints just outside the old range test's
        // windows, get an empty range — isPictographBase rejects these so
        // they don't trigger a tooltip lookup on their own.
        auto require_no_range = [](llwchar cp, const char* tag) {
            std::u32string ws { cp };
            auto r = wstring_emoji_range_at(ws, 0);
            INFO(tag);
            REQUIRE(r.first == r.second);
        };
        require_no_range((llwchar)0x200D, "bare ZWJ");
        require_no_range((llwchar)0xFE0F, "bare VS-16");
        require_no_range((llwchar)0xFE0E, "bare VS-15");
        require_no_range((llwchar)0x20E3, "bare keycap combiner");
        require_no_range((llwchar)0x1F3FB, "bare skintone");
        require_no_range((llwchar)0xE0067, "bare tag char");
        require_no_range((llwchar)0x1FFF, "U+1FFF below the old BMP window");
        require_no_range((llwchar)0x3300, "U+3300 above the old BMP window");
        require_no_range((llwchar)0x20000, "U+20000 above the old astral window");

        // Back-to-back flags 🇺🇸🇯🇵 — pos lookup must find the *second* run
        // when pos has passed the first. Exercises the ordered-iteration
        // early-out (`if (pos < run.first) break`) — must not stop early.
        std::u32string two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                                (llwchar)0x1F1EF, (llwchar)0x1F1F5 };
        auto tf0 = wstring_emoji_range_at(two_flags, 0);
        auto tf2 = wstring_emoji_range_at(two_flags, 2);
        auto tf3 = wstring_emoji_range_at(two_flags, 3);
        CHECK(tf0.first == size_t(0));
        CHECK(tf0.second == size_t(2));
        CHECK(tf2.first == size_t(2));
        CHECK(tf2.second == size_t(4));
        CHECK(tf3.first == size_t(2));
        CHECK(tf3.second == size_t(4));

        // Mixed "A 🚀 B": ASCII positions return empty, the rocket position
        // returns its [pos, pos+1) range via the single-codepoint fallback.
        std::u32string mixed = { (llwchar)'A', (llwchar)0x1F680, (llwchar)'B' };
        auto m0 = wstring_emoji_range_at(mixed, 0);
        auto m1 = wstring_emoji_range_at(mixed, 1);
        auto m2 = wstring_emoji_range_at(mixed, 2);
        CHECK(m0.first == m0.second);
        CHECK(m1.first == size_t(1));
        CHECK(m1.second == size_t(2));
        CHECK(m2.first == m2.second);
    }

    // ---------------------------------------------------------------
    // wstring_grapheme_align_backward / _forward: snap onto cluster
    // boundary only when pos is *strictly inside* a cluster. Existing
    // word-walk callers (LLTextEditor::prevWordPos, LLLineEditor::*WordPos)
    // depend on positions already on a boundary being returned unchanged;
    // setCursorPos newly depends on mid-cluster snap. Pin both halves of
    // the contract so neither group regresses.
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: grapheme alignment moves only positions inside a cluster", "[LLStringUTF]")
    {
        // ASCII: every position is already on a boundary, so both halves
        // are identity functions across the entire range.
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        for (size_t p = 0; p <= ascii.size(); ++p)
        {
            CAPTURE(p);
            REQUIRE(wstring_grapheme_align_backward(ascii, p) == p);
            REQUIRE(wstring_grapheme_align_forward(ascii, p) == p);
        }

        // ZWJ family "H 👨 ZWJ 👩 ZWJ 👧 i" — cluster spans [1, 6).
        std::u32string fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };

        // Endpoint short-circuits: pos == 0 stays 0 (backward); pos >= size
        // stays size (forward) regardless of trailing emoji.
        CHECK(wstring_grapheme_align_backward(fam, 0) == size_t(0));
        CHECK(wstring_grapheme_align_backward(fam, fam.size()) == fam.size());
        CHECK(wstring_grapheme_align_forward(fam, fam.size()) == fam.size());
        CHECK(wstring_grapheme_align_forward(fam, 99) == fam.size());

        // pos == run.first — strictly outside, returned unchanged. This is
        // the contract LLTextEditor::prevWordPos relies on: a word-walk
        // that already landed on a cluster start is fine and must not be
        // pulled backward into something else.
        CHECK(wstring_grapheme_align_backward(fam, 1) == size_t(1));
        CHECK(wstring_grapheme_align_forward(fam, 1) == size_t(1));
        // pos == run.second — also strictly outside, unchanged.
        CHECK(wstring_grapheme_align_backward(fam, 6) == size_t(6));
        CHECK(wstring_grapheme_align_forward(fam, 6) == size_t(6));

        // pos strictly inside [2..5] — backward snaps to 1, forward to 6.
        for (size_t p = 2; p <= 5; ++p)
        {
            CAPTURE(p);
            REQUIRE(wstring_grapheme_align_backward(fam, p) == size_t(1));
            REQUIRE(wstring_grapheme_align_forward(fam, p) == size_t(6));
        }

        // Outside the cluster — surrounding ASCII positions unchanged.
        CHECK(wstring_grapheme_align_backward(fam, 7) == size_t(7));
        CHECK(wstring_grapheme_align_forward(fam, 7) == size_t(7));

        // Two disjoint clusters in one string — flag at [1,3), family at
        // [4,9). Inside-the-second-cluster lookups must reach the second
        // run despite the first run being earlier in the iteration. Pins
        // that the early-out (`if (pos <= run.first) break`) doesn't
        // terminate prematurely.
        std::u32string two = { (llwchar)'H',
                          (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                          (llwchar)' ',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'!' };
        // Flag interior (pos == 2) → flag.first 1.
        CHECK(wstring_grapheme_align_backward(two, 2) == size_t(1));
        CHECK(wstring_grapheme_align_forward(two, 2) == size_t(3));
        // Family interior (pos == 6) → family bounds [4, 9).
        CHECK(wstring_grapheme_align_backward(two, 6) == size_t(4));
        CHECK(wstring_grapheme_align_forward(two, 6) == size_t(9));
        // Between the two clusters (the space at pos 3) — unchanged.
        CHECK(wstring_grapheme_align_backward(two, 3) == size_t(3));
        CHECK(wstring_grapheme_align_forward(two, 3) == size_t(3));
    }

    // ---------------------------------------------------------------
    // step_grapheme + emoji_range_at: malformed-input pins. The simple
    // ZWJ-family / flag cases are covered by the cases above;
    // here we cover the rougher edges that show up in user-pasted text:
    // empty strings, out-of-bounds positions, clusters at the very ends
    // of the buffer, two clusters back-to-back with no separator, and
    // hit-test positions sitting on tag chars / extenders inside a
    // cluster (where the caller wants the whole cluster's range, not a
    // synthetic singleton range).
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: grapheme stepping and emoji ranges in edge cases", "[LLStringUTF]")
    {
        // Empty wstring — no grapheme moves possible.
        std::u32string empty;
        CHECK(wstring_step_grapheme_forward(empty, 0) == size_t(0));
        CHECK(wstring_step_grapheme_backward(empty, 0) == size_t(0));

        // Out-of-bounds position — both directions clamp into [0, size], as the
        // header says. The old walker returned pos-1 here, handing a caller an
        // index past the end of its own string.
        std::u32string ascii = { (llwchar)'a', (llwchar)'b' };
        CHECK(wstring_step_grapheme_forward(ascii, 99) == size_t(2));
        CHECK(wstring_step_grapheme_backward(ascii, 99) == size_t(2));

        // Cluster at start of string — backward from inside or just-past
        // snaps to 0 (cluster.first).
        std::u32string lead = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'X' };
        CHECK(wstring_step_grapheme_forward(lead, 0) == size_t(2));
        CHECK(wstring_step_grapheme_backward(lead, 2) == size_t(0));

        // Cluster at end of string — forward from inside snaps to size.
        std::u32string trail = { (llwchar)'X', (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        CHECK(wstring_step_grapheme_forward(trail, 1) == size_t(3));
        CHECK(wstring_step_grapheme_backward(trail, trail.size()) == size_t(1));

        // Two clusters back-to-back with no separator: step_forward from
        // inside cluster A must land at A.end (which is also B.first), not
        // skip into cluster B.
        std::u32string two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                                (llwchar)0x1F1EF, (llwchar)0x1F1F5 };
        CHECK(wstring_step_grapheme_forward(two_flags, 0) == size_t(2));
        CHECK(wstring_step_grapheme_forward(two_flags, 2) == size_t(4));
        CHECK(wstring_step_grapheme_backward(two_flags, 4) == size_t(2));
        CHECK(wstring_step_grapheme_backward(two_flags, 2) == size_t(0));

        // emoji_range_at on a tag char inside a subdivision flag — the
        // hit-test caller wants the whole flag's range so the tooltip key
        // covers the composed glyph, not the raw tag byte.
        std::u32string subdiv = { (llwchar)'<',
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
            CAPTURE(p);
            REQUIRE(r.first == size_t(1));
            REQUIRE(r.second == size_t(8));
        }
        // pos on the surrounding ASCII falls outside the cluster.
        auto pre  = wstring_emoji_range_at(subdiv, 0);
        auto post = wstring_emoji_range_at(subdiv, 8);
        CHECK(pre.first == pre.second);
        CHECK(post.first == post.second);
    }

    // ---------------------------------------------------------------
    // Pin overload-equivalence: the no-clusters and with-clusters forms
    // of utf8str_emoji_range_at must produce identical results for the
    // same input. The with-clusters overload exists to let callers
    // amortise the utf8str_find_emoji_clusters scan across many lookups,
    // so any future change that updates one body without the other
    // (e.g. a bug fix in the loop) needs to fail loudly here.
    // ---------------------------------------------------------------

    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_emoji_range_at answers the same given the cluster list", "[LLStringUTF]")
    {
        // Same mixed-content string as the align case above: ASCII + flag +
        // space + ZWJ family + ASCII. Two disjoint clusters at [1,3) and
        // [4,9), so the loop bodies exercise both early-out and middle-
        // of-vector iteration.
        std::u32string two = { (llwchar)'H',
                          (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                          (llwchar)' ',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'!' };
        const std::string utf8 = to_utf8(two);
        const auto clusters = utf8str_find_emoji_clusters(utf8);
        CHECK(clusters.size() == size_t(2));

        // Handing the list in has to give the same answer as building it
        // inside, at every position including the ones inside a character.
        for (size_t p = 0; p <= utf8.size(); ++p)
        {
            auto r1 = utf8str_emoji_range_at(utf8, p);
            auto r2 = utf8str_emoji_range_at(utf8, p, clusters);
            CAPTURE(p);
            REQUIRE(r1.first == r2.first);
            REQUIRE(r1.second == r2.second);
        }

        // Empty string -- pinning that the with-clusters overload handles a
        // degenerate empty cluster vector identically to the no-clusters form.
        const EmojiClusterList no_clusters;
        CHECK(utf8str_emoji_range_at(std::string_view(), 0).first ==
                      utf8str_emoji_range_at(std::string_view(), 0, no_clusters).first);
    }

    // utf8str_substChar replaces every occurrence of a code point (incl.
    // astrals) with another code point, decoding and re-encoding each one so
    // the byte-length change is handled for us.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_substChar replaces every occurrence of a code point", "[LLStringUTF]")
    {
        const std::string in  = "A\xF0\x9F\x9A\x80" "B\xF0\x9F\x9A\x80" "C";
        const std::string out = utf8str_substChar(in, (llwchar)0x1F680, (llwchar)'X');
        CHECK(out == std::string("AXBXC"));

        const std::string in2  = "A\xE6\x97\xA5" "B";
        const std::string out2 = utf8str_substChar(in2, (llwchar)0x65E5, (llwchar)'.');
        CHECK(out2 == std::string("A.B"));
    }

    // utf8str_makeASCII replaces every non-ASCII llwchar (>0x7F) with '?'
    // before serializing back to utf-8.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_makeASCII turns each non-ASCII character into a question mark", "[LLStringUTF]")
    {
        const std::string mixed = "A\xE6\x97\xA5" "B\xF0\x9F\x9A\x80" "C";
        const std::string expected = "A?B?C";
        CHECK(utf8str_makeASCII(mixed) == expected);
        CHECK(utf8str_makeASCII("Hello") == std::string("Hello"));
    }

    // mbcsstring_makeASCII is byte-level: every byte >0x7F becomes '?'. So a
    // 3-byte UTF-8 character yields three '?' rather than one.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: mbcsstring_makeASCII turns each non-ASCII byte into a question mark", "[LLStringUTF]")
    {
        const std::string mixed = "A\xE6\x97\xA5" "B";
        const std::string expected = "A???B";
        CHECK(mbcsstring_makeASCII(mixed) == expected);
    }

    // utf8str_removeCRLF strips CR (0x0D) only — LF is preserved.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_removeCRLF strips CR and keeps LF", "[LLStringUTF]")
    {
        CHECK(utf8str_removeCRLF("a\r\nb") == std::string("a\nb"));
        CHECK(utf8str_removeCRLF("a\rb") == std::string("ab"));
        CHECK(utf8str_removeCRLF("a\nb") == std::string("a\nb"));
        CHECK(utf8str_removeCRLF("") == std::string());
    }

    // utf8str_split splits at the last split-token at or before maxlen bytes,
    // falling back to a hard byte cut when no token is in range. Non-final
    // chunks retain the trailing split-token (current implementation).
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_split splits at the last split-token at or before maxlen bytes", "[LLStringUTF]")
    {
        {
            std::list<std::string> out;
            utf8str_split(out, "Hello there world", 10, ' ');
            REQUIRE(out.size() == size_t(3));
            auto it = out.begin();
            CHECK(*it++ == std::string("Hello"));
            CHECK(*it++ == std::string("there "));
            CHECK(*it == std::string("world"));
        }
        {
            std::list<std::string> out;
            utf8str_split(out, "short", 10, ' ');
            REQUIRE(out.size() == size_t(1));
            CHECK(out.front() == std::string("short"));
        }
    }

    // wchar_utf8_preview formats the code point in hex, plus (when multi-byte)
    // the decoded UTF-8 byte sequence also in hex.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: wchar_utf8_preview gives the code point and its UTF-8 bytes in hex", "[LLStringUTF]")
    {
        CHECK(wchar_utf8_preview((llwchar)'A') == std::string("41"));
        // é = U+00E9 -> 0xC3 0xA9
        CHECK(wchar_utf8_preview((llwchar)0x00E9) ==
                      std::string("E9 [C3, A9]"));
        // Rocket = U+1F680 -> 0xF0 0x9F 0x9A 0x80
        CHECK(wchar_utf8_preview((llwchar)0x1F680) ==
                      std::string("1F680 [F0, 9F, 9A, 80]"));
    }

    // The grapheme walkers moved from an emoji-only cluster list to full
    // UAX #29 via ICU. These are the cases the old walker stepped
    // through one codepoint at a time.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: grapheme stepping follows UAX #29 beyond emoji", "[LLStringUTF]")
    {
        // "e" + COMBINING ACUTE, then "x". The base and its mark are one
        // cluster; the old walker split them.
        const std::u32string combining = { (llwchar)'e', (llwchar)0x0301, (llwchar)'x' };
        CHECK(wstring_step_grapheme_forward(combining, 0) == size_t(2));
        CHECK(wstring_step_grapheme_backward(combining, 2) == size_t(0));
        CHECK(wstring_grapheme_align_backward(combining, 1) == size_t(0));
        CHECK(wstring_grapheme_align_forward(combining, 1) == size_t(2));

        // Hangul LVT: HANGUL SYLLABLE inputs as jamo L + V + T are one cluster.
        const std::u32string hangul = { (llwchar)0x1100, (llwchar)0x1161, (llwchar)0x11A8, (llwchar)'!' };
        CHECK(wstring_step_grapheme_forward(hangul, 0) == size_t(3));
        CHECK(wstring_step_grapheme_backward(hangul, 3) == size_t(0));

        // Regional indicator pairs still pair up, and a third RI starts a new
        // cluster rather than joining the first two.
        const std::u32string flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1FA };
        CHECK(wstring_step_grapheme_forward(flags, 0) == size_t(2));
        CHECK(wstring_step_grapheme_forward(flags, 2) == size_t(3));

        // ZWJ family stays one cluster, as before.
        const std::u32string family = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                                   (llwchar)0x200D, (llwchar)0x1F467, (llwchar)'!' };
        CHECK(wstring_step_grapheme_forward(family, 0) == size_t(5));
        CHECK(wstring_step_grapheme_backward(family, 5) == size_t(0));

        // GB4 breaks after LF, which is also what bounds the backward scan.
        const std::u32string lines = { (llwchar)'a', (llwchar)'\n', (llwchar)'e', (llwchar)0x0301 };
        CHECK(wstring_step_grapheme_backward(lines, 2) == size_t(1));
        UNSCOPED_INFO("cluster after lf");
        CHECK(wstring_step_grapheme_backward(lines, 4) == size_t(2));

        // Degenerate input: empty, and positions past the end clamp.
        const std::u32string empty;
        CHECK(wstring_step_grapheme_forward(empty, 0) == size_t(0));
        CHECK(wstring_step_grapheme_backward(empty, 0) == size_t(0));
        CHECK(wstring_step_grapheme_forward(combining, 99) == combining.size());
        CHECK(wstring_grapheme_align_forward(combining, 99) == combining.size());

        // Stepping forward from every position must strictly advance, and
        // backward must strictly retreat, or a caret can wedge.
        for (size_t p = 0; p < family.size(); ++p)
        {
            CAPTURE(p);
            REQUIRE(wstring_step_grapheme_forward(family, p) > p);
        }
        for (size_t p = 1; p <= family.size(); ++p)
        {
            CAPTURE(p);
            REQUIRE(wstring_step_grapheme_backward(family, p) < p);
        }
    }

    // Word stepping lands on the start of a word, stepping over the whitespace
    // between. UAX #29 via ICU, replacing an alnum-or-underscore test.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: word stepping lands on the start of each word", "[LLStringUTF]")
    {
        const std::u32string two_words = to_u32("foo bar");
        CHECK(wstring_step_word_forward(two_words, 0) == size_t(4));
        CHECK(wstring_step_word_forward(two_words, 4) == size_t(7));
        CHECK(wstring_step_word_backward(two_words, 7) == size_t(4));
        CHECK(wstring_step_word_backward(two_words, 4) == size_t(0));
        UNSCOPED_INFO("back from mid-word");
        CHECK(wstring_step_word_backward(two_words, 5) == size_t(4));

        // A full stop between letters does not split the word -- UAX #29 wants
        // example.com and 3.14 to hold together (WB6/WB7, FULL STOP is
        // MidNumLet). So the word here is "foo.bar" and the next one is "baz".
        const std::u32string dotted = to_u32("foo.bar baz");
        CHECK(wstring_step_word_forward(dotted, 0) == size_t(8));
        UNSCOPED_INFO("fwd from inside the dot word");
        CHECK(wstring_step_word_forward(dotted, 3) == size_t(8));

        // A comma does separate, and standing on one used to wedge the cursor:
        // it is neither alnum nor a space, so both of the old loops declined to
        // move and the caller had to retry from pos+1 (LLLineEditor::removeWord
        // still carries that workaround).
        const std::u32string comma = to_u32("foo, bar");
        UNSCOPED_INFO("fwd stops on the comma");
        CHECK(wstring_step_word_forward(comma, 0) == size_t(3));
        UNSCOPED_INFO("fwd off the comma");
        CHECK(wstring_step_word_forward(comma, 3) == size_t(5));
        UNSCOPED_INFO("fwd never stands still");
        CHECK(wstring_step_word_forward(comma, 3) > size_t(3));

        // An apostrophe is inside the word, not a break in it. The old walk
        // stopped at index 3, mid-word.
        const std::u32string contraction = to_u32("don't stop");
        CHECK(wstring_step_word_forward(contraction, 0) == size_t(6));
        UNSCOPED_INFO("back over a contraction");
        CHECK(wstring_step_word_backward(contraction, 10) == size_t(6));

        // Tabs are whitespace to step over; the old test was == ' ' only.
        const std::u32string tabbed = to_u32("foo\tbar");
        CHECK(wstring_step_word_forward(tabbed, 0) == size_t(4));

        // Neither direction crosses a newline, matching what the old walk did
        // by accident (a newline is neither a word char nor a space).
        const std::u32string lines = to_u32("ab\ncd");
        UNSCOPED_INFO("fwd stops at eol");
        CHECK(wstring_step_word_forward(lines, 0) == size_t(2));
        UNSCOPED_INFO("fwd parked on eol");
        CHECK(wstring_step_word_forward(lines, 2) == size_t(2));
        UNSCOPED_INFO("back stops at bol");
        CHECK(wstring_step_word_backward(lines, 5) == size_t(3));
        UNSCOPED_INFO("back parked on bol");
        CHECK(wstring_step_word_backward(lines, 3) == size_t(3));

        // Trailing whitespace has no word after it, so the line's end is where
        // forward stops.
        const std::u32string trailing = to_u32("foo   ");
        CHECK(wstring_step_word_forward(trailing, 0) == size_t(6));

        // Degenerate input.
        const std::u32string empty;
        CHECK(wstring_step_word_forward(empty, 0) == size_t(0));
        CHECK(wstring_step_word_backward(empty, 0) == size_t(0));
        CHECK(wstring_step_word_forward(two_words, 99) == two_words.size());
        CHECK(wstring_step_word_backward(two_words, 99) == size_t(4));

        // Scripts without spaces still segment, rather than swallowing the
        // whole run the way an alnum test would.
        const std::u32string cjk = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E ok");
        CHECK(wstring_step_word_forward(cjk, 0) > size_t(0));
        CHECK(wstring_step_word_forward(cjk, 0) <= cjk.size());

        // Adjacent emoji are separate words, so a word step crosses exactly
        // one. Callers must not pre-step a grapheme and then ask for the next
        // word -- that lands two emoji away.
        const std::u32string two_emoji = { (llwchar)0x1F436, (llwchar)0x1F431, (llwchar)'x' };
        CHECK(wstring_step_word_forward(two_emoji, 0) == size_t(1));
        CHECK(wstring_step_word_backward(two_emoji, 2) == size_t(1));

        // A ZWJ sequence is a single word, however many codepoints it spans.
        const std::u32string flag_then_dog = { (llwchar)0x1F3F3, (llwchar)0xFE0F, (llwchar)0x200D,
                                          (llwchar)0x26A7, (llwchar)0xFE0F, (llwchar)0x1F436 };
        CHECK(wstring_step_word_forward(flag_then_dog, 0) == size_t(5));
        CHECK(wstring_step_word_backward(flag_then_dog, 6) == size_t(5));
    }

    // UAX #14 line break opportunities, which replaced a spaces-plus-CJK-range
    // heuristic in LLFontGL::maxDrawableBytes.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: line break opportunities follow UAX #14", "[LLStringUTF]")
    {
        std::vector<size_t> breaks;

        // A break is offered after the space, so the line keeps its trailing
        // space and the next one starts on the word. This is what the old
        // start_of_last_word tracking computed, and it has to stay that way.
        wstring_line_break_opportunities(to_u32("hello world"), breaks);
        REQUIRE(breaks.size() == size_t(2));
        CHECK(breaks[0] == size_t(6));
        CHECK(breaks[1] == size_t(11));

        // The string's end is always an opportunity; 0 never is.
        wstring_line_break_opportunities(to_u32("unbroken"), breaks);
        REQUIRE(breaks.size() == size_t(1));
        CHECK(breaks[0] == size_t(8));

        // A non-breaking space is glue. The old code special-cased U+00A0 by
        // hand; here it simply produces no opportunity.
        wstring_line_break_opportunities(to_u32("a\xC2\xA0" "b"), breaks);
        REQUIRE(breaks.size() == size_t(1));
        CHECK(breaks[0] == size_t(3));

        // CJK ideographs may be split between characters...
        const std::u32string cjk = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        wstring_line_break_opportunities(cjk, breaks);
        CHECK(breaks.size() > size_t(1));

        // ...but a line may not begin with closing punctuation, so there is no
        // opportunity immediately before U+3002 IDEOGRAPHIC FULL STOP. The old
        // ideograph-range test would have broken there happily.
        const std::u32string cjk_stop = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE3\x80\x82");
        wstring_line_break_opportunities(cjk_stop, breaks);
        CHECK(std::find(breaks.begin(), breaks.end(), size_t(2)) == breaks.end());

        // Every opportunity is in range and strictly ascending, since the
        // consumer walks them with a single forward cursor.
        const std::u32string mixed = to_u32("one two\xC2\xA0three, four");
        wstring_line_break_opportunities(mixed, breaks);
        REQUIRE_FALSE(breaks.empty());
        for (size_t k = 0; k < breaks.size(); ++k)
        {
            CAPTURE(k);
            REQUIRE(breaks[k] > 0);
            REQUIRE(breaks[k] <= mixed.size());
            if (k > 0)
            {
                REQUIRE(breaks[k] > breaks[k - 1]);
            }
        }
        CHECK(breaks.back() == mixed.size());

        // Empty input clears the caller's buffer rather than leaving it alone,
        // since the buffer is reused across lines.
        wstring_line_break_opportunities(std::u32string(), breaks);
        CHECK(breaks.empty());
    }

    // Case conversion, and the index map that lets a search fold its haystack
    // without losing track of where a match sits in the original.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: case conversion and mapping offsets back from a folded copy", "[LLStringUTF]")
    {
        // Uppercasing sharp s produces two characters. towupper could not do
        // this at all -- it maps one codepoint to one codepoint.
        std::string sharp_s = "stra\xC3\x9F" "e";
        CHECK(sharp_s.size() == size_t(7));
        LLStringUtil::toUpper(sharp_s);
        CHECK(sharp_s == std::string("STRASSE"));

        std::string plain = "Hello";
        LLStringUtil::toLower(plain);
        CHECK(plain == std::string("hello"));

        // U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE lowercases to two
        // characters, so every offset after it shifts. This is the case that
        // breaks an offset taken from a folded copy.
        const std::string dotted_i = "a\xC4\xB0" "b";
        REQUIRE(dotted_i.size() == size_t(4));

        std::string folded = dotted_i;
        LLStringUtil::toLower(folded);
        CHECK(folded.size() > dotted_i.size());

        // The point of all this: find 'b' in the folded copy and land on the
        // right byte in the original. Taken raw, the offset would be past it.
        const size_t folded_at = folded.find('b');
        REQUIRE(folded_at != std::string::npos);
        CHECK(folded_at != size_t(3));
        CHECK(utf8str_bytes_from_cased_bytes(dotted_i, folded_at, false) == size_t(3));

        // Every prefix of the fold maps back onto a character boundary of the
        // original, and never past its end.
        for (size_t k = 0; k <= folded.size(); ++k)
        {
            const size_t back = utf8str_bytes_from_cased_bytes(dotted_i, k, false);
            CAPTURE(k);
            REQUIRE(back <= dotted_i.size());
            REQUIRE((back == dotted_i.size()
                   || (((unsigned char)dotted_i[back]) & 0xC0) != 0x80));
        }

        // ASCII stays one-to-one, so an offset needs no mapping at all.
        const std::string ascii = "ABC";
        for (size_t k = 0; k <= ascii.size(); ++k)
        {
            CAPTURE(k);
            REQUIRE(utf8str_bytes_from_cased_bytes(ascii, k, false) == k);
        }

        // Sharp s changes the character count without changing the byte
        // count: two bytes of ß become two bytes of SS. The totals matching is
        // what makes it a trap -- offset 5 sits between the two S's, which is
        // no position in the original at all, and the answer backs off to
        // before the ß rather than inventing one.
        const std::string strasse = "stra\xC3\x9F" "e";
        std::string upper = strasse;
        LLStringUtil::toUpper(upper);
        REQUIRE(upper.size() == strasse.size());
        CHECK(utf8str_codepoint_count(upper) == utf8str_codepoint_count(strasse) + 1);

        const size_t back_from[] = { 0, 1, 2, 3, 4, 4, 6, 7 };
        for (size_t k = 0; k <= upper.size(); ++k)
        {
            CAPTURE(k);
            REQUIRE(utf8str_bytes_from_cased_bytes(strasse, k, true) == back_from[k]);
        }
    }

    // The narrow forms case UTF-8 now, and the helper that brings an offset
    // taken against a cased copy back onto the text it was built from.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: narrow case conversion and offsets mapped back from a cased key", "[LLStringUTF]")
    {
        std::string s = "stra\xC3\x9F" "e";
        LLStringUtil::toUpper(s);
        CHECK(s == std::string("STRASSE"));

        std::string acc = "\xC3\xA9" "cole";
        LLStringUtil::toUpper(acc);
        CHECK(acc == std::string("\xC3\x89" "COLE"));

        std::string upper = "\xC3\x89" "COLE";
        LLStringUtil::toLower(upper);
        CHECK(upper == std::string("\xC3\xA9" "cole"));

        // ASCII is untouched, which is what the identifier and protocol callers
        // rely on.
        std::string ascii = "Image.PNG";
        LLStringUtil::toLower(ascii);
        CHECK(ascii == std::string("image.png"));

        // The ff ligature uppercases to "FF": three bytes become two, so an
        // offset taken from the key sits BEFORE where the same text starts in
        // the label. This is the case the mapping exists for.
        {
            const std::string label = "a\xEF\xAC\x80" "b xyz";
            std::string key = label;
            LLStringUtil::toUpper(key);
            const size_t at = key.find("XYZ");
            REQUIRE(at != std::string::npos);
            CHECK(key.size() < label.size());

            // xyz begins at byte 6 of the label and byte 5 of the key.
            CHECK(utf8str_bytes_from_cased_bytes(label, at, true) == size_t(6));
            CHECK(at != size_t(6));
        }

        // "straße" uppercases to "STRASSE": one codepoint longer, but the same
        // number of bytes, because the sharp s already took two. In bytes the
        // offset maps straight through -- which is the point of counting them.
        const std::string label = "stra\xC3\x9F" "e xyz";
        std::string key = label;
        LLStringUtil::toUpper(key);
        const size_t at = key.find("XYZ");
        REQUIRE(at != std::string::npos);
        CHECK(key.size() == label.size());

        const size_t mapped = utf8str_bytes_from_cased_bytes(label, at, true);
        CHECK(mapped == size_t(8));

        // And the matched span's own length, which is not the key's.
        const size_t span_begin = utf8str_bytes_from_cased_bytes(label, 0, true);
        const size_t span_end = utf8str_bytes_from_cased_bytes(label, key.find(' '), true);
        CHECK(span_end - span_begin == size_t(7));

        // Offset 0 and a whole-string offset are the degenerate ends.
        CHECK(utf8str_bytes_from_cased_bytes(label, 0, true) == size_t(0));
        CHECK(utf8str_bytes_from_cased_bytes(label, key.size(), true) == size_t(11));

        // ASCII text maps one-to-one, so existing offsets are unaffected.
        const std::string plain = "hello world";
        CHECK(utf8str_bytes_from_cased_bytes(plain, 6, true) == size_t(6));

        CHECK(utf8str_bytes_from_cased_bytes(std::string(), 4, true) == size_t(0));
    }

    // "Which word is here" and "where is the next one", the questions
    // double-click selection, spell check and autoreplace ask.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: the word at a position and the next word from it", "[LLStringUTF]")
    {
        const std::u32string text = to_u32("don't stop, ok");

        // A contraction is one word. The alnum-or-underscore walk this replaced
        // stopped at the apostrophe and offered "don" or "t".
        auto word = wstring_word_range_at(text, 0);
        CHECK(word.first == size_t(0));
        CHECK(word.second == size_t(5));

        // Anywhere inside it gives the same word.
        for (size_t p = 0; p < 5; ++p)
        {
            const auto at = wstring_word_range_at(text, p);
            CAPTURE(p);
            REQUIRE(at.first == size_t(0));
            REQUIRE(at.second == size_t(5));
        }

        // Whitespace and punctuation are segments too, and neither is a word.
        const auto space = wstring_word_range_at(text, 5);
        CHECK(space.first == space.second);
        const auto comma = wstring_word_range_at(text, 10);
        CHECK(comma.first == comma.second);

        // Iterating: each call yields the next word, skipping what is between.
        std::vector<std::string> found;
        size_t at = 0;
        while (at < text.size())
        {
            const auto next = wstring_next_word_range(text, at);
            if (next.first >= next.second)
                break;
            found.push_back(to_utf8(text.substr(next.first, next.second - next.first)));
            at = next.second;
        }
        REQUIRE(found.size() == size_t(3));
        CHECK(found[0] == std::string("don't"));
        CHECK(found[1] == std::string("stop"));
        CHECK(found[2] == std::string("ok"));

        // Iteration crosses lines, which the spell checker relies on.
        const std::u32string lines = to_u32("one\ntwo");
        const auto first = wstring_next_word_range(lines, 0);
        CHECK(first.second == size_t(3));
        const auto second = wstring_next_word_range(lines, first.second);
        CHECK(second.first == size_t(4));
        CHECK(second.second == size_t(7));

        // Past the end, and past the last word, both terminate.
        const auto none = wstring_next_word_range(lines, 7);
        CHECK(none.first == none.second);
        const auto oob = wstring_word_range_at(lines, 99);
        CHECK(oob.first == oob.second);

        const std::u32string empty;
        const auto on_empty = wstring_word_range_at(empty, 0);
        CHECK(on_empty.first == on_empty.second);

        // A whole string that is one word -- what autoreplace validates a
        // keyword with.
        const std::u32string keyword = to_u32("don't");
        const auto whole = wstring_word_range_at(keyword, 0);
        CHECK(whole.first == size_t(0));
        CHECK(whole.second == keyword.size());
        const std::u32string two_words = to_u32("not one");
        const auto partial = wstring_word_range_at(two_words, 0);
        CHECK(partial.second != two_words.size());
    }

    // Classification above the BMP. Every one of these codepoints used to be
    // handed to <cwctype>, whose wint_t is 16 bits on Windows -- so each
    // arrived with its top half cut off and was answered for by whatever
    // happened to live at the remaining address.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: classification and case above the BMP", "[LLStringUTF]")
    {
        // The low half of each is given as the answer it used to produce.
        const llwchar CJK_EXT_B_A   = 0x2000A;  // -> U+000A, a line feed
        const llwchar CJK_EXT_B_ONE = 0x20031;  // -> U+0031, digit one
        const llwchar LINEAR_B      = 0x10020;  // -> U+0020, a space
        const llwchar DESERET_CAP   = 0x10401;  // -> U+0401, Cyrillic Io
        const llwchar DESERET_SMALL = 0x10429;
        const llwchar MATH_BOLD_A   = 0x1D400;  // -> U+D400, a lone surrogate
        const llwchar GRINNING_FACE = 0x1F600;

        CHECK_FALSE(LLStringOps::isSpace(CJK_EXT_B_A));
        CHECK(LLStringOps::isAlnum(CJK_EXT_B_A));
        CHECK_FALSE(LLStringOps::isDigit(CJK_EXT_B_ONE));
        CHECK(LLStringOps::isAlnum(CJK_EXT_B_ONE));
        CHECK_FALSE(LLStringOps::isSpace(LINEAR_B));
        CHECK(LLStringOps::isAlpha(MATH_BOLD_A));
        CHECK_FALSE(LLStringOps::isAlnum(GRINNING_FACE));
        CHECK_FALSE(LLStringOps::isSpace(GRINNING_FACE));

        // Case above the BMP: this used to return an unrelated codepoint
        // rather than fail to convert.
        // char32_t has no operator<<, so the comparands are widened for the
        // failure message rather than compared as they stand.
        const auto cased = [](llwchar c) { return (U32)c; };

        CHECK(LLStringOps::isUpper(DESERET_CAP));
        CHECK(cased(LLStringOps::toLower(DESERET_CAP)) == cased(DESERET_SMALL));
        CHECK(cased(LLStringOps::toUpper(DESERET_SMALL)) == cased(DESERET_CAP));

        // Case inside the BMP, which towlower left alone entirely.
        UNSCOPED_INFO("Greek sigma lowercases");
        CHECK(cased(LLStringOps::toLower(llwchar(0x03A3))) == cased(0x03C3));
        UNSCOPED_INFO("Cyrillic Io lowercases");
        CHECK(cased(LLStringOps::toLower(llwchar(0x0401))) == cased(0x0451));
        UNSCOPED_INFO("Cyrillic io uppercases");
        CHECK(cased(LLStringOps::toUpper(llwchar(0x0451))) == cased(0x0401));

        // Whitespace is the Unicode property, not a guess.
        UNSCOPED_INFO("no-break space is whitespace");
        CHECK(LLStringOps::isSpace(llwchar(0x00A0)));
        UNSCOPED_INFO("ideographic space is whitespace");
        CHECK(LLStringOps::isSpace(llwchar(0x3000)));
        UNSCOPED_INFO("zero width space is not");
        CHECK_FALSE(LLStringOps::isSpace(llwchar(0x200B)));

        // Digits are ASCII on purpose -- every caller reads the run as a number.
        UNSCOPED_INFO("Arabic-Indic zero is not a digit");
        CHECK_FALSE(LLStringOps::isDigit(llwchar(0x0660)));
        UNSCOPED_INFO("Arabic-Indic zero is alphanumeric");
        CHECK(LLStringOps::isAlnum(llwchar(0x0660)));

        // ASCII keeps answering as it always did.
        CHECK(LLStringOps::isUpper(llwchar('A')));
        CHECK(LLStringOps::isLower(llwchar('a')));
        CHECK(LLStringOps::isDigit(llwchar('5')));
        CHECK(LLStringOps::isSpace(llwchar(' ')));
        CHECK(LLStringOps::isPunct(llwchar('.')));
        CHECK(cased(LLStringOps::toLower(llwchar('A'))) == cased(llwchar('a')));
    }

    // The consequence of the above for the cursor: a CJK ideograph whose low
    // half is a line feed must not read as a run of whitespace to step over.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: an astral ideograph is a word to the cursor and not a gap", "[LLStringUTF]")
    {
        std::u32string text = to_u32("ab");
        text.push_back(0x2000A);
        text.push_back(0x2000A);
        text += to_u32(" cd");

        // Each ideograph is its own segment, and none of them is whitespace,
        // so the cursor stops at the first. Reading their low halves as line
        // feeds made all three of the ideographs and the space look like one
        // gap, and the cursor jumped clear to "cd" at 5.
        CHECK(wstring_step_word_forward(text, 0) == size_t(2));

        const auto range = wstring_word_range_at(text, 2);
        CHECK(range.first != range.second);
    }

    // utf8str_grapheme_align_backward: the byte offsets are the caller's own,
    // and a cut backs off to where a whole character starts. Everything that
    // trims UTF-8 to a length leans on this.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_grapheme_align_backward backs off to a character start", "[LLStringUTF]")
    {
        // ASCII: every offset is already a boundary.
        const std::string ascii = "abcd";
        for (size_t i = 0; i <= ascii.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(utf8str_grapheme_align_backward(ascii, i) == i);
        }

        // Inside a multi-byte codepoint, back to where it starts. "A" + U+65E5
        // (3 bytes) + "B".
        const std::string cjk = "A\xE6\x97\xA5" "B";
        UNSCOPED_INFO("before the codepoint");
        CHECK(utf8str_grapheme_align_backward(cjk, 1) == size_t(1));
        UNSCOPED_INFO("inside it, byte 2");
        CHECK(utf8str_grapheme_align_backward(cjk, 2) == size_t(1));
        UNSCOPED_INFO("inside it, byte 3");
        CHECK(utf8str_grapheme_align_backward(cjk, 3) == size_t(1));
        UNSCOPED_INFO("after it");
        CHECK(utf8str_grapheme_align_backward(cjk, 4) == size_t(4));

        // A base and its combining mark are one character: "e" + U+0301.
        const std::string combining = "e\xCC\x81" "x";
        UNSCOPED_INFO("between base and mark");
        CHECK(utf8str_grapheme_align_backward(combining, 1) == size_t(0));
        UNSCOPED_INFO("inside the mark");
        CHECK(utf8str_grapheme_align_backward(combining, 2) == size_t(0));
        UNSCOPED_INFO("after the whole cluster");
        CHECK(utf8str_grapheme_align_backward(combining, 3) == size_t(3));

        // A flag is two codepoints and eight bytes, and one character. Cutting
        // anywhere inside gives nothing rather than half a flag.
        const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        for (size_t i = 1; i < flag.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(utf8str_grapheme_align_backward(flag, i) == size_t(0));
        }
        CHECK(utf8str_grapheme_align_backward(flag, flag.size()) == flag.size());

        // Degenerate input.
        CHECK(utf8str_grapheme_align_backward(cjk, 0) == size_t(0));
        CHECK(utf8str_grapheme_align_backward(cjk, 99) == cjk.size());
        CHECK(utf8str_grapheme_align_backward(std::string(), 0) == size_t(0));
    }

    // The walkers work in UTF-8, so every boundary is written out here in
    // bytes. Text with a byte per codepoint, three, two, and a two-codepoint
    // cluster, so byte and codepoint offsets disagree everywhere after the
    // first space.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: the UTF-8 walkers find every boundary in mixed text", "[LLStringUTF]")
    {
        const std::string utf8 =
            "ab "                                   // ascii
            "\xE6\x97\xA5\xE6\x9C\xAC"              // CJK, three bytes each
            " e\xCC\x81 "                           // e + combining acute
            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"      // flag: two codepoints, one cluster
            " end";
        // a=0 b=1 sp=2 CJK=3,6 sp=9 e=10 acute=11 sp=13 flag=14 sp=22 end=23.
        REQUIRE(utf8.size() == size_t(26));

        // Every grapheme boundary, written out. The combining acute and the
        // two regional indicators are the ones a byte walk would split.
        const size_t boundaries[] = { 0, 1, 2, 3, 6, 9, 10, 13, 14, 22, 23, 24, 25, 26 };
        const size_t count = sizeof(boundaries) / sizeof(boundaries[0]);

        size_t at = 0;
        for (size_t i = 1; i < count; ++i)
        {
            at = utf8str_step_grapheme_forward(utf8, at);
            CAPTURE(i);
            REQUIRE(at == boundaries[i]);
        }
        CHECK(at == utf8.size());

        for (size_t i = count - 1; i > 0; --i)
        {
            at = utf8str_step_grapheme_backward(utf8, at);
            CAPTURE(i);
            REQUIRE(at == boundaries[i - 1]);
        }
        CHECK(at == size_t(0));

        // Every offset inside a cluster snaps to that cluster's edges.
        for (size_t i = 1; i < count; ++i)
        {
            for (size_t inside = boundaries[i - 1] + 1; inside < boundaries[i]; ++inside)
            {
                CAPTURE(i, inside);
                REQUIRE(utf8str_grapheme_align_backward(utf8, inside) ==
                              boundaries[i - 1]);
                REQUIRE(utf8str_grapheme_align_forward(utf8, inside) ==
                              boundaries[i]);
            }
        }

        // Word stepping lands on the start of each word, never in the gaps.
        const size_t word_starts[] = { 3, 10, 14, 23, 26 };
        size_t word_at = 0;
        for (size_t i = 0; i < sizeof(word_starts) / sizeof(word_starts[0]); ++i)
        {
            word_at = utf8str_step_word_forward(utf8, word_at);
            CAPTURE(i);
            REQUIRE(word_at == word_starts[i]);
        }

        // The word under the first ideograph is the ideograph pair.
        const auto cjk_word = utf8str_word_range_at(utf8, 3);
        CHECK(cjk_word.first == size_t(3));
        CHECK(cjk_word.second == size_t(9));

        // Line break opportunities: after each space, between the ideographs,
        // and at the end. Never inside a character or a flag.
        std::vector<size_t> breaks;
        utf8str_line_break_opportunities(utf8, breaks);
        REQUIRE_FALSE(breaks.empty());
        CHECK(breaks.back() == utf8.size());
        for (size_t where : breaks)
        {
            CAPTURE(where);
            REQUIRE(where != 0);
            REQUIRE(std::find(boundaries, boundaries + count, where) != boundaries + count);
        }
        CHECK(std::find(breaks.begin(), breaks.end(), size_t(6)) != breaks.end());
        CHECK(std::find(breaks.begin(), breaks.end(), size_t(18)) == breaks.end());

        // A flag is one cluster of eight bytes, and nothing cuts it in half.
        const size_t flag = utf8.find("\xF0\x9F\x87\xBA");
        CHECK(utf8str_step_grapheme_forward(utf8, flag) == flag + 8);
        for (size_t inside = 1; inside < 8; ++inside)
        {
            CAPTURE(inside);
            REQUIRE(utf8str_grapheme_align_backward(utf8, flag + inside) == flag);
            REQUIRE(utf8str_grapheme_align_forward(utf8, flag + inside) == flag + 8);
        }
    }

    // ---------------------------------------------------------------
    //                 12x  utf8 walkers
    // ---------------------------------------------------------------

    // Over a corpus of text, the cluster walker finds each run at the byte
    // offsets written out for it, and no extra ones.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_find_emoji_clusters finds each run in bytes", "[LLStringUTF]")
    {
        const std::vector<std::u32string> corpus = {
            std::u32string(),
            { (llwchar)'H', (llwchar)'i', (llwchar)'!' },
            { (llwchar)0x65E5, (llwchar)0x672C },                       // CJK
            { (llwchar)0x1F680 },                                       // lone emoji
            { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
              (llwchar)0x200D, (llwchar)0x1F467 },                      // ZWJ family
            { (llwchar)0x1F468, (llwchar)0x1F3FB },                     // skin tone
            { (llwchar)0x1F1FA, (llwchar)0x1F1F8 },                     // flag
            { (llwchar)'1', (llwchar)0xFE0F, (llwchar)0x20E3 },         // keycap
            { (llwchar)0x2764, (llwchar)0xFE0F, (llwchar)0x200D,
              (llwchar)0x1F525 },                                       // heart on fire
            // ASCII, CJK, a cluster, then more ASCII: byte and codepoint
            // offsets have already diverged before the cluster starts.
            { (llwchar)'a', (llwchar)0x65E5, (llwchar)0x1F468,
              (llwchar)0x200D, (llwchar)0x1F469, (llwchar)'z' },
        };

        // Byte length of each entry, and the one cluster run it holds, written
        // out. An isolated emoji is deliberately not a run: it renders through
        // the 1:1 glyph lookup and needs no cluster.
        struct Expect { size_t bytes; bool has_run; size_t begin; size_t end; };
        const Expect expected[] = {
            { 0,  false, 0,  0},    // empty
            { 3,  false, 0,  0},    // "Hi!"
            { 6,  false, 0,  0},    // CJK
            { 4,  false, 0,  0},    // lone emoji
            {18,  true,  0, 18},    // ZWJ family, 4+3+4+3+4
            { 8,  true,  0,  8},    // skin tone, 4+4
            { 8,  true,  0,  8},    // flag, 4+4
            { 7,  true,  0,  7},    // keycap, 1+3+3
            {13,  true,  0, 13},    // heart on fire, 3+3+3+4
            {16,  true,  4, 15},    // a + ideograph + ZWJ pair + z
        };
        REQUIRE(corpus.size() == sizeof(expected) / sizeof(expected[0]));

        for (size_t c = 0; c < corpus.size(); ++c)
        {
            const std::string utf8 = to_utf8(corpus[c]);
            CAPTURE(c);
            REQUIRE(utf8.size() == expected[c].bytes);

            const auto runs = utf8str_find_emoji_clusters(utf8);
            REQUIRE(runs.size() == (expected[c].has_run ? size_t(1) : size_t(0)));
            if (expected[c].has_run)
            {
                REQUIRE(runs[0].first == expected[c].begin);
                REQUIRE(runs[0].second == expected[c].end);
            }
        }
    }

    // Byte coordinates pinned outright: a ZWJ pair sitting behind an
    // ideograph starts at byte 4, not codepoint 2.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: a run after an ideograph is placed in bytes not codepoints", "[LLStringUTF]")
    {
        std::u32string wide = { (llwchar)'a', (llwchar)0x65E5,
                           (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                           (llwchar)'z' };
        const std::string utf8 = to_utf8(wide);
        REQUIRE(utf8.size() == size_t(1 + 3 + 4 + 3 + 4 + 1));

        const auto runs = utf8str_find_emoji_clusters(utf8);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].first == size_t(4));
        CHECK(runs[0].second == size_t(15));

    }

    // Malformed UTF-8 must not stall the walk or invent a cluster. Every bad
    // byte decodes to U+FFFD over one byte, which matches no rule.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: malformed UTF-8 neither stalls the walk nor invents a cluster", "[LLStringUTF]")
    {
        UNSCOPED_INFO("lone continuation");
        CHECK(utf8str_find_emoji_clusters("\x80\x80\x80").size() == size_t(0));
        UNSCOPED_INFO("truncated four-byte");
        CHECK(utf8str_find_emoji_clusters("\xF0\x9F\x91").size() == size_t(0));
        UNSCOPED_INFO("bad lead");
        CHECK(utf8str_find_emoji_clusters("\xFF\xFE").size() == size_t(0));

        // A bad byte in front of a real cluster still leaves the cluster
        // findable, at the offset that byte pushed it to.
        std::string text = "\xFF";
        text += to_utf8(std::u32string{ (llwchar)0x1F468, (llwchar)0x200D,
                                              (llwchar)0x1F469 });
        const auto runs = utf8str_find_emoji_clusters(text);
        REQUIRE(runs.size() == size_t(1));
        CHECK(runs[0].first == size_t(1));
        CHECK(runs[0].second == text.size());
    }

    // utf8str_emoji_range_at answered at every position in the string --
    // including the ones inside a character, which belong to no pictograph.
    // The expected ranges are written out rather than derived, so a change that
    // moved the lookup and the walk the same wrong way is still caught.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_emoji_range_at answers at every byte position", "[LLStringUTF]")
    {
        const std::u32string wide = { (llwchar)'a',                    // 1 byte
                                 (llwchar)0x2764, (llwchar)0xFE0F,// heart + VS16
                                 (llwchar)0x65E5,                 // ideograph
                                 (llwchar)0x1F468, (llwchar)0x200D,
                                 (llwchar)0x1F469,                // ZWJ family
                                 (llwchar)0x00A9,                 // lone (c)
                                 (llwchar)'z' };
        const std::string utf8 = to_utf8(wide);

        // a=0 heart=1 VS16=4 ideograph=7 man=10 ZWJ=14 woman=17 (c)=21 z=23.
        REQUIRE(utf8.size() == size_t(24));

        // The range every byte offset belongs to, written out. Inside a
        // cluster every byte reports the whole cluster, character start or
        // not -- a hit test lands on a pixel. Outside one, an empty range is
        // spelled as the position itself: 'a' and the ideograph are not
        // pictographs, and a byte inside a character decodes to no character
        // at all.
        struct Expect { size_t at; size_t begin; size_t end; };
        const Expect expected[] = {
            { 0,  0,  0}, { 1,  1,  7}, { 2,  1,  7}, { 3,  1,  7},
            { 4,  1,  7}, { 5,  1,  7}, { 6,  1,  7}, { 7,  7,  7},
            { 8,  8,  8}, { 9,  9,  9}, {10, 10, 21}, {11, 10, 21},
            {12, 10, 21}, {13, 10, 21}, {14, 10, 21}, {15, 10, 21},
            {16, 10, 21}, {17, 10, 21}, {18, 10, 21}, {19, 10, 21},
            {20, 10, 21}, {21, 21, 23}, {22, 22, 22}, {23, 23, 23},
            {24, 24, 24},
        };
        for (const Expect& e : expected)
        {
            const auto got = utf8str_emoji_range_at(utf8, e.at);
            CAPTURE(e.at);
            REQUIRE(got.first == e.begin);
            REQUIRE(got.second == e.end);
        }

        // The heart carries a presentation selector, so it is a cluster and
        // its range spans both codepoints -- three bytes plus three.
        const auto heart = utf8str_emoji_range_at(utf8, 1);
        CHECK(heart.first == size_t(1));
        CHECK(heart.second == size_t(7));

        // The lone copyright sign is no cluster, but it is a pictograph base,
        // so it reports its own two bytes rather than an empty range.
        const size_t copy_at = 1 + 6 + 3 + 11;
        const auto copyright = utf8str_emoji_range_at(utf8, copy_at);
        CHECK(copyright.first == copy_at);
        CHECK(copyright.second == copy_at + 2);

        // A position inside the ideograph is not a character start and so
        // belongs to nothing.
        const auto split = utf8str_emoji_range_at(utf8, 8);
        CHECK(split.first == split.second);

        // Past the end reports an empty range at the position asked about,
        // not at the end of the string.
        const auto past = utf8str_emoji_range_at(utf8, utf8.size() + 4);
        CHECK(past.first == past.second);
    }

    // utf8str_codepoint_count is what a limit expressed in characters gets
    // compared against, so it has to agree with the UTF-32 form's size().
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_codepoint_count agrees with the UTF-32 size", "[LLStringUTF]")
    {
        CHECK(utf8str_codepoint_count("") == size_t(0));
        CHECK(utf8str_codepoint_count("abc") == size_t(3));

        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)0x200D, (llwchar)0x1F600 };
        const std::string utf8 = to_utf8(wide);
        CHECK(utf8.size() == size_t(1 + 2 + 3 + 4 + 3 + 4));
        CHECK(utf8str_codepoint_count(utf8) == wide.size());

        // Malformed bytes count one each, so the count never disagrees with a
        // walk over the same text.
        UNSCOPED_INFO("lone continuation bytes");
        CHECK(utf8str_codepoint_count("\x80\x80") == size_t(2));
        UNSCOPED_INFO("truncated sequence");
        CHECK(utf8str_codepoint_count("\xF0\x9F") == size_t(2));
    }

    // The UTF-16 pair converts between code units and bytes: the Win32 IME
    // hands offsets in code units and the editors index bytes, so a wrong
    // answer here is a caret landing in the wrong place mid-composition.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: UTF-16 unit counts and byte counts convert both ways", "[LLStringUTF]")
    {
        // 'a', e-acute, an ideograph, an astral emoji (a surrogate pair), 'z'.
        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)'z' };
        const std::string utf8 = to_utf8(wide);

        // Bytes 1 + 2 + 3 + 4 + 1; code units 1 + 1 + 1 + 2 + 1.
        REQUIRE(utf8.size() == size_t(11));
        CHECK(utf8str_utf16_length(utf8, 0, S32_MAX) == S32(6));

        // Every prefix, by hand: the byte offset of each character start and
        // the code units the text up to it occupies.
        const S32 byte_at[] = { 0, 1, 3, 6, 10, 11 };
        const S32 unit_at[] = { 0, 1, 2, 3,  5,  6 };
        for (size_t cp = 0; cp < 6; ++cp)
        {
            CAPTURE(cp);
            REQUIRE(utf8str_utf16_length(utf8, 0, byte_at[cp]) == unit_at[cp]);
        }

        // Back the other way: a budget in code units becomes a byte count, and
        // a budget that stops between the emoji's surrogate halves says so.
        const S32 bytes_for_units[] = { 0, 1, 3, 6, 6, 10, 11 };
        for (S32 units = 0; units <= 6; ++units)
        {
            bool unaligned = false;
            CAPTURE(units);
            REQUIRE(utf8str_length_from_utf16_length(utf8, 0, units, &unaligned) ==
                          bytes_for_units[units]);
            REQUIRE(unaligned == (units == 4));
        }

        // Offsets are honoured, not just whole-string calls.
        CHECK(utf8str_utf16_length(utf8, 3, S32_MAX) == S32(4));

        // Out of range clamps rather than running off.
        CHECK(utf8str_utf16_length(utf8, 999, 999) == S32(0));
        CHECK(utf8str_length_from_utf16_length(utf8, 0, 0) == S32(0));
    }

    // Valid text has to come back byte-identical -- the clipboard calls this on
    // every paste, and a sanitizer that rewrites what was already correct would
    // be worse than none.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_sanitize returns valid text byte for byte", "[LLStringUTF]")
    {
        const std::string ascii = "plain ascii";
        CHECK(utf8str_sanitize(ascii) == ascii);

        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)'z' };
        const std::string mixed = to_utf8(wide);
        CHECK(utf8str_sanitize(mixed) == mixed);

        CHECK(utf8str_sanitize(std::string_view()) == std::string());

        // Embedded NUL is valid UTF-8 and must survive; string_view carries it.
        const std::string with_nul("a\0b", 3);
        CHECK(utf8str_sanitize(with_nul) == with_nul);
    }

    // And malformed text has to come back valid, whatever shape the damage took.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_sanitize makes malformed text valid", "[LLStringUTF]")
    {
        // Against simdutf, not against the function under test. Defining
        // validity as "sanitize leaves it alone" only asks whether the repair
        // is a fixed point -- it cannot fail for a repair that returns
        // something invalid, so long as it does so consistently.
        const auto is_valid = [](const std::string& s)
        {
            return simdutf::validate_utf8(s.data(), s.size());
        };

        // A lone continuation byte, a truncated lead, an overlong 'A', an
        // encoded surrogate half, and a value past U+10FFFF. simdutf rejects
        // the last three even though a naive decoder would accept them.
        const std::string bad[] = {
            std::string("ok\x80" "ok"),
            std::string("ok\xE6\x97" "ok"),
            std::string("ok\xC1\x81" "ok"),
            std::string("ok\xED\xA0\x80" "ok"),
            std::string("ok\xF5\x80\x80\x80" "ok"),
        };

        for (const std::string& s : bad)
        {
            UNSCOPED_INFO("input really is malformed");
            REQUIRE_FALSE(is_valid(s));
            // The predicate the widgets ask before deciding whether to repair
            // has to agree with simdutf, or a widget skips a repair it needed.
            REQUIRE(utf8str_is_valid(s) == is_valid(s));
            const std::string fixed = utf8str_sanitize(s);
            REQUIRE(is_valid(fixed));
            REQUIRE(utf8str_is_valid(fixed));
            REQUIRE(fixed.find("ok") == 0);
            REQUIRE(fixed.rfind("ok") == fixed.size() - 2);
        }
    }

    // A decoder that accepts more than the encoding allows lets a character
    // reach somewhere a filter already looked for it.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_decode_at accepts only what UTF-8 allows", "[LLStringUTF]")
    {
        struct { const char* bytes; const char* what; } bad[] = {
            { "\xC0\xAF",         "overlong slash" },
            { "\xC0\x80",         "overlong NUL" },
            { "\xE0\x80\x80",     "overlong three-byte NUL" },
            { "\xED\xA0\x80",     "encoded surrogate half" },
            { "\xF5\x80\x80\x80", "past U+10FFFF" },
            { "\xF7\xBF\xBF\xBF", "well past U+10FFFF" },
            { "\x80",             "lone continuation" },
            { "\xFF",             "no such lead" },
        };

        for (const auto& c : bad)
        {
            const std::string s(c.bytes);
            const LLCodepointAt at = utf8str_decode_at(s, 0);
            INFO(c.what);
            REQUIRE((S32)at.cp == (S32)0xFFFD);
            REQUIRE((S32)at.next == 1);
        }

        // The shapes that are legal still decode whole.
        CHECK((S32)utf8str_decode_at(std::string("A"), 0).cp == (S32)'A');
        CHECK((S32)utf8str_decode_at(std::string("\xC3\xA9"), 0).cp == (S32)0xE9);
        CHECK((S32)utf8str_decode_at(std::string("\xE6\x97\xA5"), 0).cp == (S32)0x65E5);
        CHECK((S32)utf8str_decode_at(std::string("\xF0\x9F\x90\xB6"), 0).cp == (S32)0x1F436);
        CHECK((S32)utf8str_decode_at(std::string("\xF0\x9F\x90\xB6"), 0).next == (S32)4);
    }

    // Shortening text must not empty it. The budget counts codepoints while the
    // cut lands on a cluster, so a first cluster that spends more codepoints
    // than the budget allows has nowhere to fall back to except itself.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_symbol_truncate never cuts text to nothing", "[LLStringUTF]")
    {
        // A ZWJ family: five codepoints, eighteen bytes, one cluster.
        const std::string family("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
        const std::string text = family + "abc";

        for (S32 budget = 1; budget <= 5; ++budget)
        {
            const std::string cut = utf8str_symbol_truncate(text, budget);
            CAPTURE(budget);
            REQUIRE_FALSE(cut.empty());
            REQUIRE(cut == family);
        }

        // Once the budget clears the cluster the next characters come too.
        CHECK(utf8str_symbol_truncate(text, 6) == family + "a");
        CHECK(utf8str_symbol_truncate(std::string("abc"), 10) == std::string("abc"));
    }

    // Case-insensitive over ASCII compares bytes, so a spelling the format
    // never allowed cannot pass for the one it did.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: LLStringUtil::isEqualInsensitiveASCII folds ASCII and nothing else", "[LLStringUTF]")
    {
        CHECK(LLStringUtil::isEqualInsensitiveASCII(std::string("llsd/binary"), std::string("LLSD/Binary")));
        CHECK_FALSE(LLStringUtil::isEqualInsensitiveASCII(std::string("llsd"), std::string("llsd/binary")));
        // Fullwidth 'l' is its own character, whatever a collator makes of it.
        CHECK_FALSE(LLStringUtil::isEqualInsensitiveASCII(
                   std::string("\xEF\xBD\x8C\xEF\xBD\x8Csd/binary"), std::string("llsd/binary")));
        // A soft hyphen is a character, not an absence of one.
        CHECK_FALSE(LLStringUtil::isEqualInsensitiveASCII(
                   std::string("llsd/bin\xC2\xAD" "ary"), std::string("llsd/binary")));
        // Above ASCII nothing is folded -- these are simply different bytes.
        CHECK_FALSE(LLStringUtil::isEqualInsensitiveASCII(
                   std::string("caf\xC3\xA9"), std::string("caf\xC3\x89")));
        CHECK(LLStringUtil::isEqualInsensitiveASCII(std::string(), std::string()));
    }

    // The word walkers driven by their own offsets. The tests beside these go
    // through this file's wstring_* adapters, which turn codepoint indexes
    // into bytes and back -- not a byte offset supplied by a caller, which is
    // what every caller in llui now passes.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: the UTF-8 word walkers read byte offsets", "[LLStringUTF]")
    {
        // ASCII first, so the byte answers can be read against the wide test.
        const std::string two_words = "foo bar";
        CHECK(utf8str_step_word_forward(two_words, 0) == size_t(4));
        CHECK(utf8str_step_word_forward(two_words, 4) == size_t(7));
        CHECK(utf8str_step_word_backward(two_words, 7) == size_t(4));
        CHECK(utf8str_step_word_backward(two_words, 4) == size_t(0));

        // And where a character is not a byte. "café bar": the accent makes
        // the second word start at byte 6 where it is codepoint 5, so these
        // numbers are wrong under any reading but the byte one.
        const std::string accented = "caf\xC3\xA9 bar";
        REQUIRE(accented.size() == size_t(9));
        CHECK(utf8str_step_word_forward(accented, 0) == size_t(6));
        CHECK(utf8str_step_word_forward(accented, 6) == size_t(9));
        CHECK(utf8str_step_word_backward(accented, 9) == size_t(6));
        CHECK(utf8str_step_word_backward(accented, 6) == size_t(0));

        // An offset inside the accent is still inside the first word.
        CHECK(utf8str_step_word_forward(accented, 4) == size_t(6));

        // The walkers stay on their line -- LLTextEditor::nextWordPos crosses
        // the break itself, and would stall on every newline if this changed.
        const std::string lines = "ab\ncd";
        CHECK(utf8str_step_word_forward(lines, 0) == size_t(2));
        CHECK(utf8str_step_word_forward(lines, 2) == size_t(2));
        CHECK(utf8str_step_word_backward(lines, 3) == size_t(3));
    }

    // Word ranges in bytes: what a double-click selects, and what comes next.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: word ranges in bytes for selection and for the next word", "[LLStringUTF]")
    {
        const std::string accented = "caf\xC3\xA9 bar";

        // Anywhere inside the first word reports the whole of it, including a
        // position inside the accent.
        for (size_t at : { size_t(0), size_t(2), size_t(3), size_t(4) })
        {
            const auto range = utf8str_word_range_at(accented, at);
            CAPTURE(at);
            REQUIRE(range.first == size_t(0));
            REQUIRE(range.second == size_t(5));
        }

        const auto second = utf8str_word_range_at(accented, 7);
        CHECK(second.first == size_t(6));
        CHECK(second.second == size_t(9));

        // next_word_range finds the word whose end lies past the position --
        // the one the position is in, when it is in one, and only otherwise
        // the following word. From inside the first word that is still the
        // first word.
        const auto from_start = utf8str_next_word_range(accented, 0);
        CHECK(from_start.first == size_t(0));
        CHECK(from_start.second == size_t(5));

        // Standing on the space, the answer is the word after it.
        const auto from_space = utf8str_next_word_range(accented, 5);
        CHECK(from_space.first == size_t(6));
        CHECK(from_space.second == size_t(9));

        // Past the last word there is nothing further to select.
        const auto none = utf8str_next_word_range(accented, accented.size());
        CHECK(none.first == accented.size());
        CHECK(none.second == accented.size());
    }

    // The caret forms differ from the raw walkers in exactly one way: they
    // cross a line break instead of stalling on it. Both text widgets forward
    // to these rather than carrying the policy twice, so it is worth pinning
    // the difference rather than only the agreement.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: the caret word walkers cross line breaks the raw ones stop at", "[LLStringUTF]")
    {
        const std::string lines = "one two\nthree four";

        // Away from a break the two are the same walk.
        CHECK(utf8str_caret_word_forward(lines, 0) ==
                      utf8str_step_word_forward(lines, 0));
        CHECK(utf8str_caret_word_backward(lines, 7) ==
                      utf8str_step_word_backward(lines, 7));

        // A caret already at the edge of a word must not skip the adjacent
        // word, and a caret inside a word must land at that word's start.
        const std::string adjacent = "foo bar";
        CHECK(utf8str_caret_word_forward(adjacent, 3) == size_t(4));
        CHECK(utf8str_caret_word_backward(adjacent, 5) == size_t(4));

        // On the break the raw walker has nowhere to go and says so.
        CHECK(utf8str_step_word_forward(lines, 7) == size_t(7));
        CHECK(utf8str_caret_word_forward(lines, 7) > size_t(7));

        // And at a line start, going back.
        CHECK(utf8str_step_word_backward(lines, 8) == size_t(8));
        CHECK(utf8str_caret_word_backward(lines, 8) < size_t(8));

        // Neither runs off either end.
        CHECK(utf8str_caret_word_backward(lines, 0) == size_t(0));
        CHECK(utf8str_caret_word_forward(lines, lines.size()) == lines.size());

        // Repeated stepping terminates rather than parking.
        size_t at = 0;
        for (int i = 0; i < 32 && at < lines.size(); ++i)
        {
            const size_t next = utf8str_caret_word_forward(lines, at);
            CAPTURE(at);
            REQUIRE(next >= at);
            if (next == at) break;
            at = next;
        }
        CHECK(at == lines.size());
    }

    // ---------------------------------------------------------------
    //                              trim
    // ---------------------------------------------------------------

    // LLStringUtil::trim answers for Unicode whitespace, not just ASCII. A
    // no-break space arrives pasted out of a web page and an ideographic
    // space sits either side of CJK; a byte-wise isspace() saw neither.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: LLStringUtil::trim answers for Unicode whitespace", "[LLStringUTF]")
    {
        const std::string nbsp   = "\xC2\xA0";      // U+00A0
        const std::string ideo   = "\xE3\x80\x80";  // U+3000
        const std::string em     = "\xE2\x80\x83";  // U+2003
        const std::string narrow = "\xE2\x80\xAF";  // U+202F
        const std::string cjk    = "\xE6\x97\xA5\xE6\x9C\xAC";

        std::string s = nbsp + "Rye" + nbsp;
        LLStringUtil::trim(s);
        CHECK(s == std::string("Rye"));

        s = ideo + cjk + ideo;
        LLStringUtil::trim(s);
        CHECK(s == cjk);

        s = em + narrow + " \tx\t " + narrow + em;
        LLStringUtil::trim(s);
        CHECK(s == std::string("x"));

        s = nbsp + "a";
        LLStringUtil::trimHead(s);
        CHECK(s == std::string("a"));

        s = "a" + nbsp;
        LLStringUtil::trimTail(s);
        CHECK(s == std::string("a"));

        s = nbsp + ideo + em;
        LLStringUtil::trim(s);
        CHECK(s.empty());
    }

    // What is not White_Space stays, and malformed bytes are content: a trim
    // that guessed at them could cut a character in half.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: trim keeps what is not White_Space and malformed bytes", "[LLStringUTF]")
    {
        const std::string zwsp = "\xE2\x80\x8B";  // U+200B, not White_Space
        const std::string bom  = "\xEF\xBB\xBF";  // U+FEFF, not White_Space

        std::string s = zwsp + "a" + zwsp;
        LLStringUtil::trim(s);
        CHECK(s == zwsp + "a" + zwsp);

        s = bom + "a" + bom;
        LLStringUtil::trim(s);
        CHECK(s == bom + "a" + bom);

        s = std::string("\x80") + "a" + std::string("\x80");
        LLStringUtil::trim(s);
        CHECK(s.size() == size_t(3));

        // U+3000 missing its last byte. The lead byte it shares with a real
        // ideographic space must not be enough to cut it.
        s = std::string("a") + "\xE3\x80";
        LLStringUtil::trim(s);
        CHECK(s.size() == size_t(3));

        s = "  hello  ";
        LLStringUtil::trim(s);
        CHECK(s == std::string("hello"));

        s.clear();
        LLStringUtil::trim(s);
        CHECK(s.empty());
    }

    // The view forms cut where the string forms cut, and utf8str_trim shares
    // that decision rather than keeping a second one.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: the view forms cut where the string forms cut", "[LLStringUTF]")
    {
        const std::string src = std::string("\xC2\xA0") + "\t" + "caf\xC3\xA9" + "\xE3\x80\x80";
        const std::string expected = "caf\xC3\xA9";

        std::string owned = src;
        LLStringUtil::trim(owned);
        CHECK(owned == expected);

        std::string_view view = src;
        LLStringUtil::trim(view);
        CHECK(std::string(view) == expected);

        CHECK(utf8str_trim(src) == expected);
    }

    // Where each cluster starts, walked at once, is where stepping one at a
    // time stops: the combining acute and the flag's two indicators kept
    // with what they belong to; none past the end asked for.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_grapheme_starts stops where stepping stops", "[LLStringUTF]")
    {
        const std::string utf8 =
            "ab "
            "\xE6\x97\xA5\xE6\x9C\xAC"
            " e\xCC\x81 "
            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"
            " end";
        std::vector<size_t> stepped;
        for (size_t at = 0; at < utf8.size(); at = utf8str_step_grapheme_forward(utf8, at))
        {
            stepped.push_back(at);
        }
        CHECK(utf8str_grapheme_starts(utf8, utf8.size()) == stepped);
        CHECK(utf8str_grapheme_starts(utf8, 11) == std::vector<size_t>({ 0, 1, 2, 3, 6, 9, 10 }));
        CHECK(utf8str_grapheme_starts(std::string(), 5).empty());
        CHECK(utf8str_grapheme_starts(utf8, 0).empty());
    }

    // Every word at once is every word utf8str_next_word_range finds one at
    // a time from the start: over lines, contractions, numbers, scripts
    // that do not space their words, combining marks, emoji, blank lines
    // and nothing at all.
    TEST_CASE_METHOD(llstring_utf_data, "LLStringUTF: utf8str_word_ranges finds the words next_word_range does", "[LLStringUTF]")
    {
        const std::string texts[] = {
            "",
            "   ",
            "one",
            "don't stop, it's 3.14 and a_b or e.g. x:y",
            "first line\nsecond  line\r\n\nfourth\n",
            "\n\nafter blanks",
            "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xA7\xE3\x81\x99 mixed",
            "caf\xC3\xA9 e\xCC\x81t\xC3\xA9 na\xC3\xAFve",
            "flag \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 then words",
            "    // Counts the parts of b, and more for a long one.",
            "trailing words   ",
        };
        for (const std::string& text : texts)
        {
            std::vector<std::pair<size_t, size_t>> stepped;
            for (size_t at = 0;;)
            {
                const auto word = utf8str_next_word_range(text, at);
                if (word.first >= word.second)
                {
                    break;
                }
                stepped.push_back(word);
                at = word.second;
            }
            std::vector<std::pair<size_t, size_t>> all = { { 9, 9 } };
            utf8str_word_ranges(text, all);
            CAPTURE(text);
            REQUIRE(all == stepped);
        }
        std::vector<std::pair<size_t, size_t>> words;
        utf8str_word_ranges("don't stop", words);
        CHECK(words == std::vector<std::pair<size_t, size_t>>({ { 0, 5 }, { 6, 10 } }));
    }
}
