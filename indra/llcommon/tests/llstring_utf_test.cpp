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

    // llutf16string element types have no std::ostream operator<<, so
    // ensure_equals fails to instantiate its failure formatter. Compare by
    // value and format diagnostics ourselves.
    void ensure_wstring_equals(const std::string& msg,
                               const std::u32string& actual,
                               const std::u32string& expected)
    {
        if (actual == expected) return;

        std::ostringstream oss;
        oss << msg << ": std::u32string mismatch (actual.size=" << actual.size()
            << " expected.size=" << expected.size() << ")\n  actual: ";
        oss << std::hex << std::uppercase;
        for (llwchar c : actual) oss << "U+" << (U32)c << ' ';
        oss << "\n  expect: ";
        for (llwchar c : expected) oss << "U+" << (U32)c << ' ';
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
    //   12x utf8 walkers     (byte-offset segmentation against its wide twin)
    //   14x trim             (Unicode whitespace, and what is not whitespace)
    // The TUT default registers only test<1>..test<50>, but the explicit
    // test_group<..., 145> below raises that ceiling. Keep this index in
    // sync with categories used below.
    typedef test_group<llstring_utf_data, 145> llstring_utf_t;
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
        const std::u32string     wascii (ascii.begin(), ascii.end());
        const llutf16string u16ascii(ascii.begin(), ascii.end());

        ensure_wstring_equals  ("utf8->wstring ASCII",  to_u32 (ascii),   wascii);
        ensure_equals          ("wstring->utf8 ASCII",  to_utf8 (wascii),  ascii);
        ensure_equals          ("utf16->utf8 ASCII",    utf16str_to_utf8str(u16ascii), ascii);
    }

    // Latin-1 BMP: 2-byte UTF-8 (é = U+00E9 → C3 A9).
    template<> template<>
    void llstring_utf_object_t::test<2>()
    {
        const std::string   utf8 = "H\xC3\xA9llo";
        const std::u32string     w    = { (llwchar)'H', (llwchar)0x00E9, (llwchar)'l', (llwchar)'l', (llwchar)'o' };
        const llutf16string u16  = { (char16_t)'H', (char16_t)0x00E9, (char16_t)'l', (char16_t)'l', (char16_t)'o' };

        ensure_wstring_equals  ("utf8->wstring 2-byte",  to_u32 (utf8), w);
        ensure_equals          ("wstring->utf8 2-byte",  to_utf8 (w),    utf8);
        ensure_equals          ("utf16->utf8 2-byte",    utf16str_to_utf8str(u16),  utf8);
    }

    // CJK BMP: 3-byte UTF-8 (日 = U+65E5 → E6 97 A5, 本 = U+672C → E6 9C AC).
    template<> template<>
    void llstring_utf_object_t::test<3>()
    {
        const std::string   utf8 = "\xE6\x97\xA5\xE6\x9C\xAC";
        const std::u32string     w    = { (llwchar)0x65E5, (llwchar)0x672C };
        const llutf16string u16  = { (char16_t)0x65E5, (char16_t)0x672C };

        ensure_wstring_equals  ("utf8->wstring 3-byte",  to_u32 (utf8), w);
        ensure_equals          ("wstring->utf8 3-byte",  to_utf8 (w),    utf8);
        ensure_equals          ("utf16->utf8 3-byte",    utf16str_to_utf8str(u16),  utf8);
    }

    // Astral: 4-byte UTF-8 (U+1F680 rocket → F0 9F 9A 80; UTF-16 surrogate pair D83D DE80).
    template<> template<>
    void llstring_utf_object_t::test<4>()
    {
        const std::string   utf8 = "\xF0\x9F\x9A\x80";
        const std::u32string     w    = { (llwchar)0x1F680 };
        const llutf16string u16  = { (char16_t)0xD83D, (char16_t)0xDE80 };

        ensure_wstring_equals  ("utf8->wstring astral",  to_u32 (utf8), w);
        ensure_equals          ("wstring->utf8 astral",  to_utf8 (w),    utf8);
        ensure_equals          ("utf16->utf8 astral",    utf16str_to_utf8str(u16),  utf8);
    }

    // Mixed: ASCII + 3-byte BMP + 4-byte astral. Uses explicit string-literal
    // concatenation between astral bytes and the trailing 'B' so \xA5B and
    // \x80B don't eat an extra hex digit.
    template<> template<>
    void llstring_utf_object_t::test<5>()
    {
        const std::string   utf8 = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        const std::u32string     w    = { (llwchar)'A', (llwchar)0x65E5, (llwchar)0x1F680, (llwchar)'B' };
        const llutf16string u16  = { (char16_t)'A', (char16_t)0x65E5, (char16_t)0xD83D, (char16_t)0xDE80, (char16_t)'B' };

        ensure_wstring_equals  ("utf8->wstring mixed",  to_u32 (utf8), w);
        ensure_equals          ("wstring->utf8 mixed",  to_utf8 (w),    utf8);
        ensure_equals          ("utf16->utf8 mixed",    utf16str_to_utf8str(u16),  utf8);
    }

    // Empty inputs produce empty outputs in every direction.
    template<> template<>
    void llstring_utf_object_t::test<6>()
    {
        ensure_equals("utf8->wstring empty",  to_u32 (std::string()).size(),   size_t(0));
        ensure_equals("wstring->utf8 empty",  to_utf8 (std::u32string()),             std::string());
        ensure_equals("utf16->utf8 empty",    utf16str_to_utf8str(llutf16string()),        std::string());
    }

    // Round-trip via rawstr_to_utf8 (utf8 -> std::u32string -> utf8) preserves valid input.
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
        for (const std::string& bad : { std::string("\x80"), std::string("\xFF") })
        {
            ensure("rejected outright", !utf8str_is_valid(bad));

            // The decoder gives one replacement character per bad byte, so a
            // walk driven off it always advances.
            const std::u32string w = to_u32(bad);
            ensure_equals("one bad byte, one character", w.size(), size_t(1));
            ensure_equals("decodes to U+FFFD", (U32)w[0], U32(0xFFFD));

            // Repair substitutes LL_UNKNOWN_CHAR, and the result is valid.
            const std::string fixed = utf8str_sanitize(bad);
            ensure_equals("repaired to one byte", fixed, std::string(1, LL_UNKNOWN_CHAR));
            ensure("repair is valid", utf8str_is_valid(fixed));
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
        const std::string overlong("\xC0\x80");
        ensure("overlong NUL rejected", !utf8str_is_valid(overlong));

        const std::u32string w = to_u32(overlong);
        ensure_equals("overlong NUL size", w.size(), size_t(2));
        ensure_equals("overlong NUL[0]", (U32)w[0], U32(0xFFFD));
        ensure_equals("overlong NUL[1]", (U32)w[1], U32(0xFFFD));

        ensure_equals("repaired byte for byte",
                      utf8str_sanitize(overlong), std::string(2, LL_UNKNOWN_CHAR));
    }

    // Truncated 3-byte sequence "A\xE6\x97" — simdutf rejects both 0xE6 (leader
    // without a full body) and 0x97 (lone continuation), so the result is
    // "A??" (A + two replacement chars). Previous hand-rolled decoder
    // produced "A?".
    template<> template<>
    void llstring_utf_object_t::test<42>()
    {
        const std::string truncated("A\xE6\x97");
        ensure("truncated rejected", !utf8str_is_valid(truncated));

        const std::u32string w = to_u32(truncated);
        ensure_equals("truncated size",   w.size(), size_t(3));
        ensure_equals("truncated[0]='A'", (U32)w[0], U32('A'));
        ensure_equals("truncated[1]",     (U32)w[1], U32(0xFFFD));
        ensure_equals("truncated[2]",     (U32)w[2], U32(0xFFFD));

        ensure_equals("the good byte survives the repair",
                      utf8str_sanitize(truncated), std::string("A??"));
    }

    // Legacy 5-byte UTF-8 encoding (U+00200000 → F8 88 80 80 80) is no longer
    // accepted — RFC-3629 compliance. Each byte is flagged individually, so
    // the input becomes 5 replacement chars instead of 1 wchar with the legacy
    // codepoint.
    template<> template<>
    void llstring_utf_object_t::test<43>()
    {
        const std::string legacy("\xF8\x88\x80\x80\x80");
        ensure("5-byte legacy rejected", !utf8str_is_valid(legacy));

        const std::u32string w = to_u32(legacy);
        ensure_equals("5-byte legacy size", w.size(), size_t(5));
        for (size_t i = 0; i < w.size(); ++i)
        {
            ensure_equals("5-byte legacy[i]", (U32)w[i], U32(0xFFFD));
        }

        ensure_equals("every byte repaired",
                      utf8str_sanitize(legacy), std::string(5, LL_UNKNOWN_CHAR));
    }

    // UTF-16 lone high surrogate followed by a non-surrogate: simdutf rejects
    // the lone 0xD83D (one '?') and converts the following 0x0041 normally.
    // Previous hand-rolled decoder greedily consumed both units as one garbage
    // codepoint.
    template<> template<>
    void llstring_utf_object_t::test<44>()
    {
        const llutf16string bad = { (char16_t)0xD83D, (char16_t)0x0041 };
        const std::string out = utf16str_to_utf8str(bad);
        ensure_equals("lone surrogate + 'A' size", out.size(), size_t(2));
        ensure_equals("lone surrogate -> '?'",     (U32)(U8)out[0], U32((U8)LL_UNKNOWN_CHAR));
        ensure_equals("'A' preserved",             (U32)(U8)out[1], U32('A'));
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
        std::u32string ws = { (llwchar)'H', (llwchar)0x1F680, (llwchar)0x1F681, (llwchar)'i' };
        const std::u32string expected = { (llwchar)'H', (llwchar)'i' };
        ensure("remove_emojis returned true", wstring_remove_emojis(ws));
        ensure_wstring_equals("consecutive emojis stripped", ws, expected);

        std::u32string none = { (llwchar)'H', (llwchar)'i' };
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
        std::u32string ws = { (llwchar)'H',
                         (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467,
                         (llwchar)'i' };
        const std::u32string expected = { (llwchar)'H', (llwchar)'i' };
        ensure("ZWJ family found", wstring_remove_emojis(ws));
        ensure_wstring_equals("ZWJ family stripped", ws, expected);
    }

    // Skin-tone modifier (U+1F3FB..FF) must strip together with its base emoji.
    template<> template<>
    void llstring_utf_object_t::test<65>()
    {
        std::u32string ws = { (llwchar)'M',
                         (llwchar)0x1F468, (llwchar)0x1F3FB,
                         (llwchar)'X' };
        const std::u32string expected = { (llwchar)'M', (llwchar)'X' };
        ensure("skin tone found", wstring_remove_emojis(ws));
        ensure_wstring_equals("skin tone stripped", ws, expected);
    }

    // Regional indicator flag (🇺🇸 = U+1F1FA U+1F1F8). Both code points are in
    // the astral emoji range so both strip — the output must not leave a
    // dangling half-flag indicator.
    template<> template<>
    void llstring_utf_object_t::test<66>()
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        ensure("flag found", wstring_remove_emojis(ws));
        ensure_wstring_equals("flag stripped", ws, expected);
    }

    // Trailing VS16 (U+FE0F) after an astral emoji must be consumed together
    // with the base glyph.
    template<> template<>
    void llstring_utf_object_t::test<67>()
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0F, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        ensure("VS16 found", wstring_remove_emojis(ws));
        ensure_wstring_equals("VS16 stripped with base", ws, expected);
    }

    // Bare ZWJ or VS16 (no adjacent emoji) are not emoji themselves and must
    // be left intact — critical because ZWJ is used in Arabic/Indic shaping
    // outside any emoji context.
    template<> template<>
    void llstring_utf_object_t::test<68>()
    {
        std::u32string ws = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        const std::u32string expected = ws;
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
        std::u32string ws = { (llwchar)0x2764, (llwchar)0xFE0F };
        ensure("BMP heart+VS16 stripped", wstring_remove_emojis(ws));
        ensure_wstring_equals("BMP heart+VS16 cleared", ws, std::u32string());

        // Bare BMP heart (no VS-16) is not a cluster — single-codepoint
        // pictograph, shaped via the 1:1 path. Outside isEmoji's narrow
        // range too. Must pass through.
        std::u32string bare_heart = { (llwchar)0x2764 };
        const std::u32string bare_expected = bare_heart;
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
        std::u32string ws = { (llwchar)'A',
                         (llwchar)0x2764, (llwchar)0xFE0F,
                         (llwchar)0x200D, (llwchar)0x1F525,
                         (llwchar)'B' };
        const std::u32string expected = { (llwchar)'A', (llwchar)'B' };
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
        std::u32string digit_kc = { (llwchar)'1', (llwchar)0xFE0F, (llwchar)0x20E3, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        ensure("digit keycap found", wstring_remove_emojis(digit_kc));
        ensure_wstring_equals("digit keycap cleared", digit_kc, expected);

        std::u32string hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure("hash keycap found", wstring_remove_emojis(hash_kc));
        ensure_wstring_equals("hash keycap cleared", hash_kc, std::u32string());

        std::u32string star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure("star keycap found", wstring_remove_emojis(star_kc));
        ensure_wstring_equals("star keycap cleared", star_kc, std::u32string());

        // Bare digit (no VS-16 + combiner) must pass through. The cluster
        // walker rejects it as a starter; nothing to strip.
        std::u32string bare_digit = { (llwchar)'9' };
        const std::u32string bare_expected = bare_digit;
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
        std::u32string ws = { (llwchar)'<',
                         (llwchar)0x1F3F4,
                         (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                         (llwchar)0xE0063, (llwchar)0xE0074,
                         (llwchar)0xE007F,
                         (llwchar)'>' };
        const std::u32string expected = { (llwchar)'<', (llwchar)'>' };
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
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0E, (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
        ensure("VS-15 cluster found", wstring_remove_emojis(ws));
        ensure_wstring_equals("VS-15 cluster cleared", ws, expected);
    }

    // Two flags back-to-back 🇺🇸🇯🇵 — distinct clusters [0,2) and [2,4).
    // Both must strip cleanly; no leftover RI codepoints.
    template<> template<>
    void llstring_utf_object_t::test<74>()
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                         (llwchar)0x1F1EF, (llwchar)0x1F1F5,
                         (llwchar)'!' };
        const std::u32string expected = { (llwchar)'!' };
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
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0x200B, (llwchar)'X' };
        const std::u32string expected = { (llwchar)0x200B, (llwchar)'X' };
        ensure("rocket+ZWSP found", wstring_remove_emojis(ws));
        ensure_wstring_equals("rocket stripped, ZWSP kept", ws, expected);
    }

    // ---------------------------------------------------------------
    //                  11x utf8str helper smoke pins
    // ---------------------------------------------------------------

    // utf8str_substChar replaces every occurrence of a code point (incl.
    // astrals) with another code point, routing through std::u32string so the
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
        ensure_equals("empty",      wstring_find_emoji_clusters(std::u32string()).size(), size_t(0));
        std::u32string ascii = { (llwchar)'H', (llwchar)'i', (llwchar)'!' };
        ensure_equals("ascii",      wstring_find_emoji_clusters(ascii).size(),       size_t(0));
        std::u32string cjk   = { (llwchar)0x65E5, (llwchar)0x672C };
        ensure_equals("cjk",        wstring_find_emoji_clusters(cjk).size(),         size_t(0));
        std::u32string lone  = { (llwchar)0x1F680 };
        ensure_equals("lone emoji", wstring_find_emoji_clusters(lone).size(),        size_t(0));
        std::u32string pair  = { (llwchar)0x1F680, (llwchar)0x1F681 };
        ensure_equals("two adj em", wstring_find_emoji_clusters(pair).size(),        size_t(0));
    }

    // ZWJ family 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 — must emerge
    // as a single 5-codepoint run.
    template<> template<>
    void llstring_utf_object_t::test<91>()
    {
        std::u32string ws = { (llwchar)0x1F468, (llwchar)0x200D,
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
        std::u32string ws = { (llwchar)0x1F468, (llwchar)0x1F3FB };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("skintone runs", runs.size(),    size_t(1));
        ensure_equals("skintone end",  runs[0].second, size_t(2));
    }

    // Trailing VS16 (U+FE0F) forces emoji presentation and must be part of
    // the same shaped run as its base.
    template<> template<>
    void llstring_utf_object_t::test<93>()
    {
        std::u32string ws = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("vs16 runs", runs.size(),    size_t(1));
        ensure_equals("vs16 end",  runs[0].second, size_t(2));
    }

    // Regional indicator pair — 🇺🇸 = U+1F1FA U+1F1F8 — becomes a flag glyph
    // only under shaping. Two consecutive RIs form exactly one pair.
    template<> template<>
    void llstring_utf_object_t::test<94>()
    {
        std::u32string ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("flag runs", runs.size(),    size_t(1));
        ensure_equals("flag end",  runs[0].second, size_t(2));
        // Four RIs in a row form two separate flags.
        std::u32string four = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
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
        std::u32string digit_kc = { (llwchar)'9', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto runs = wstring_find_emoji_clusters(digit_kc);
        ensure_equals("keycap runs", runs.size(),    size_t(1));
        ensure_equals("keycap end",  runs[0].second, size_t(3));
        std::u32string hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure_equals("hash keycap", wstring_find_emoji_clusters(hash_kc).size(), size_t(1));
        std::u32string bare = { (llwchar)'5' };
        ensure_equals("bare digit",  wstring_find_emoji_clusters(bare).size(),    size_t(0));
    }

    // Subdivision flag — base 🏴 (U+1F3F4) + tag characters (U+E0020..U+E007F)
    // terminated by U+E007F. All tag bytes must be absorbed into the run.
    template<> template<>
    void llstring_utf_object_t::test<96>()
    {
        std::u32string ws = { (llwchar)0x1F3F4,
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
        std::u32string ws = { (llwchar)'H',
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
        std::u32string zwj  = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        ensure_equals("bare zwj",  wstring_find_emoji_clusters(zwj).size(),  size_t(0));
        std::u32string vs16 = { (llwchar)'a', (llwchar)0xFE0F, (llwchar)'b' };
        ensure_equals("bare vs16", wstring_find_emoji_clusters(vs16).size(), size_t(0));

        // Malformed inputs: leading/trailing extenders, lone modifiers, and
        // singletons that need a partner. Pin current behavior so future
        // refactors don't silently change cluster boundaries on broken input.
        std::u32string lead_zwj  = { (llwchar)0x200D, (llwchar)0x1F468 };
        ensure_equals("leading zwj", wstring_find_emoji_clusters(lead_zwj).size(), size_t(0));
        std::u32string trail_zwj = { (llwchar)0x1F468, (llwchar)0x200D };
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
        std::u32string base_vs16 = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto vs_runs = wstring_find_emoji_clusters(base_vs16);
        ensure_equals("base+VS16 count", vs_runs.size(), size_t(1));
        ensure_equals("base+VS16 begin", vs_runs[0].first,  size_t(0));
        ensure_equals("base+VS16 end",   vs_runs[0].second, size_t(2));
        std::u32string lone_skin = { (llwchar)0x1F3FB };
        ensure_equals("lone skintone", wstring_find_emoji_clusters(lone_skin).size(), size_t(0));
        std::u32string lone_ri = { (llwchar)0x1F1FA };
        ensure_equals("lone RI", wstring_find_emoji_clusters(lone_ri).size(), size_t(0));

        // Range boundaries on isPictographBase's astral / BMP windows.
        std::u32string just_below_astral = { (llwchar)0x1FFF };
        ensure_equals("U+1FFF no run", wstring_find_emoji_clusters(just_below_astral).size(), size_t(0));
        std::u32string just_above_bmp    = { (llwchar)0x3300 };
        ensure_equals("U+3300 no run", wstring_find_emoji_clusters(just_above_bmp).size(), size_t(0));

        // Back-to-back ZWJ families with no separator. Pin current behavior:
        // each MAN-ZWJ-WOMAN sub-sequence registers as its own cluster, so we
        // expect two runs of length 3 each.
        std::u32string two_fam = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
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
        std::u32string zwj_plus_nonemoji = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)'A' };
        ensure_equals("ZWJ+non-pictograph", wstring_find_emoji_clusters(zwj_plus_nonemoji).size(), size_t(0));

        // Double ZWJ (ZWJ + ZWJ between bases): advance hits the first
        // ZWJ, looks at the next codepoint, finds another ZWJ which is
        // not a pictograph base. Orphan-ZWJ break. Length 1, discarded.
        std::u32string double_zwj = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x200D, (llwchar)0x1F469 };
        ensure_equals("double ZWJ", wstring_find_emoji_clusters(double_zwj).size(), size_t(0));

        // Truncated subdivision flag — base 🏴 + a couple of tag bytes,
        // no U+E007F terminator. Pin: walker greedily eats every tag
        // codepoint to end-of-string. Real implementation behavior.
        std::u32string trunc_tag = { (llwchar)0x1F3F4, (llwchar)0xE0067, (llwchar)0xE0062 };
        auto trunc_runs = wstring_find_emoji_clusters(trunc_tag);
        ensure_equals("trunc tag count", trunc_runs.size(),    size_t(1));
        ensure_equals("trunc tag begin", trunc_runs[0].first,  size_t(0));
        ensure_equals("trunc tag end",   trunc_runs[0].second, size_t(3));

        // Three RIs — first two form a flag at [0,2), third RI alone is
        // not a starter (no partner). Pin: one cluster, length 2.
        std::u32string three_ri = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1F8 };
        auto three_ri_runs = wstring_find_emoji_clusters(three_ri);
        ensure_equals("three RI count", three_ri_runs.size(),    size_t(1));
        ensure_equals("three RI begin", three_ri_runs[0].first,  size_t(0));
        ensure_equals("three RI end",   three_ri_runs[0].second, size_t(2));

        // Star keycap '*' + VS-16 + U+20E3. Pin parity with digit/hash.
        std::u32string star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
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
        std::u32string ws = { (llwchar)0x2764, (llwchar)0xFE0F,
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
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        ensure_equals("ascii 0->1", wstring_step_grapheme_forward(ascii, 0), size_t(1));
        ensure_equals("ascii 2->3", wstring_step_grapheme_forward(ascii, 2), size_t(3));
        ensure_equals("ascii at end stays", wstring_step_grapheme_forward(ascii, 3), size_t(3));

        // ZWJ family in the middle: H, 👨, ZWJ, 👩, ZWJ, 👧, i
        //                           0  1    2    3    4    5    6
        std::u32string fam = { (llwchar)'H',
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
        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        ensure_equals("flag 0->past both",
                      wstring_step_grapheme_forward(flag, 0), size_t(2));
    }

    // Backward step is the mirror: one codepoint through ASCII, snap to the
    // start of any emoji cluster we'd otherwise land inside.
    template<> template<>
    void llstring_utf_object_t::test<101>()
    {
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        ensure_equals("ascii 3->2", wstring_step_grapheme_backward(ascii, 3), size_t(2));
        ensure_equals("ascii 1->0", wstring_step_grapheme_backward(ascii, 1), size_t(0));
        ensure_equals("ascii at 0 stays", wstring_step_grapheme_backward(ascii, 0), size_t(0));

        std::u32string fam = { (llwchar)'H',
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

        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
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
        std::u32string fam = { (llwchar)'H',
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
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        auto a0 = wstring_emoji_range_at(ascii, 0);
        auto a2 = wstring_emoji_range_at(ascii, 2);
        ensure_equals("ascii 0 empty", a0.first, a0.second);
        ensure_equals("ascii 2 empty", a2.first, a2.second);

        // Single-codepoint emoji 😀 (U+1F600): not in cluster list, but
        // the helper synthesises [pos, pos+1).
        std::u32string lone = { (llwchar)0x1F600 };
        auto le = wstring_emoji_range_at(lone, 0);
        ensure_equals("lone emoji.first", le.first,  size_t(0));
        ensure_equals("lone emoji.second", le.second, size_t(1));

        // Out-of-bounds and exact-end positions are empty.
        auto at_end   = wstring_emoji_range_at(ascii, 3);
        auto past_end = wstring_emoji_range_at(ascii, 99);
        ensure_equals("ascii at-end empty", at_end.first,   at_end.second);
        ensure_equals("ascii oob empty",    past_end.first, past_end.second);

        // Empty wstring: empty range.
        auto empty = wstring_emoji_range_at(std::u32string(), 0);
        ensure_equals("empty wstr empty", empty.first, empty.second);

        // Flag pair 🇺🇸 — one cluster spanning [0, 2).
        std::u32string flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
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
            std::u32string ws { cp };
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
            std::u32string ws { cp };
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
        std::u32string two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
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
        std::u32string mixed = { (llwchar)'A', (llwchar)0x1F680, (llwchar)'B' };
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
        std::u32string ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        for (size_t p = 0; p <= ascii.size(); ++p)
        {
            ensure_equals("ascii align_backward",
                          wstring_grapheme_align_backward(ascii, p), p);
            ensure_equals("ascii align_forward",
                          wstring_grapheme_align_forward(ascii, p), p);
        }

        // ZWJ family "H 👨 ZWJ 👩 ZWJ 👧 i" — cluster spans [1, 6).
        std::u32string fam = { (llwchar)'H',
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
        std::u32string two = { (llwchar)'H',
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
        std::u32string empty;
        ensure_equals("empty step_forward",  wstring_step_grapheme_forward(empty, 0),  size_t(0));
        ensure_equals("empty step_backward", wstring_step_grapheme_backward(empty, 0), size_t(0));

        // Out-of-bounds position — both directions clamp into [0, size], as the
        // header says. The old walker returned pos-1 here, handing a caller an
        // index past the end of its own string.
        std::u32string ascii = { (llwchar)'a', (llwchar)'b' };
        ensure_equals("oob step_forward",  wstring_step_grapheme_forward(ascii, 99),  size_t(2));
        ensure_equals("oob step_backward", wstring_step_grapheme_backward(ascii, 99), size_t(2));

        // Cluster at start of string — backward from inside or just-past
        // snaps to 0 (cluster.first).
        std::u32string lead = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'X' };
        ensure_equals("lead flag step_forward(0)",
                      wstring_step_grapheme_forward(lead, 0), size_t(2));
        ensure_equals("lead flag step_backward(2)",
                      wstring_step_grapheme_backward(lead, 2), size_t(0));

        // Cluster at end of string — forward from inside snaps to size.
        std::u32string trail = { (llwchar)'X', (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        ensure_equals("trail flag step_forward(1)",
                      wstring_step_grapheme_forward(trail, 1), size_t(3));
        ensure_equals("trail flag step_backward(size)",
                      wstring_step_grapheme_backward(trail, trail.size()), size_t(1));

        // Two clusters back-to-back with no separator: step_forward from
        // inside cluster A must land at A.end (which is also B.first), not
        // skip into cluster B.
        std::u32string two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
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
        std::u32string two = { (llwchar)'H',
                          (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                          (llwchar)' ',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'!' };
        const std::string utf8 = to_utf8(two);
        const auto clusters = utf8str_find_emoji_clusters(utf8);
        ensure_equals("two clusters", clusters.size(), size_t(2));

        // Handing the list in has to give the same answer as building it
        // inside, at every position including the ones inside a character.
        for (size_t p = 0; p <= utf8.size(); ++p)
        {
            auto r1 = utf8str_emoji_range_at(utf8, p);
            auto r2 = utf8str_emoji_range_at(utf8, p, clusters);
            ensure_equals("range_at first",  r1.first,  r2.first);
            ensure_equals("range_at second", r1.second, r2.second);
        }

        // Empty string -- pinning that the with-clusters overload handles a
        // degenerate empty cluster vector identically to the no-clusters form.
        const EmojiClusterList no_clusters;
        ensure_equals("empty range_at first overload",
                      utf8str_emoji_range_at(std::string_view(), 0).first,
                      utf8str_emoji_range_at(std::string_view(), 0, no_clusters).first);
    }

    // The grapheme walkers moved from an emoji-only cluster list to full
    // UAX #29 via ICU. These are the cases the old walker stepped
    // through one codepoint at a time.
    template<> template<>
    void llstring_utf_object_t::test<117>()
    {
        // "e" + COMBINING ACUTE, then "x". The base and its mark are one
        // cluster; the old walker split them.
        const std::u32string combining = { (llwchar)'e', (llwchar)0x0301, (llwchar)'x' };
        ensure_equals("combining fwd from 0", wstring_step_grapheme_forward(combining, 0), size_t(2));
        ensure_equals("combining back from 2", wstring_step_grapheme_backward(combining, 2), size_t(0));
        ensure_equals("combining align back mid", wstring_grapheme_align_backward(combining, 1), size_t(0));
        ensure_equals("combining align fwd mid", wstring_grapheme_align_forward(combining, 1), size_t(2));

        // Hangul LVT: HANGUL SYLLABLE inputs as jamo L + V + T are one cluster.
        const std::u32string hangul = { (llwchar)0x1100, (llwchar)0x1161, (llwchar)0x11A8, (llwchar)'!' };
        ensure_equals("hangul fwd from 0", wstring_step_grapheme_forward(hangul, 0), size_t(3));
        ensure_equals("hangul back from 3", wstring_step_grapheme_backward(hangul, 3), size_t(0));

        // Regional indicator pairs still pair up, and a third RI starts a new
        // cluster rather than joining the first two.
        const std::u32string flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1FA };
        ensure_equals("flag pair fwd", wstring_step_grapheme_forward(flags, 0), size_t(2));
        ensure_equals("third RI is its own", wstring_step_grapheme_forward(flags, 2), size_t(3));

        // ZWJ family stays one cluster, as before.
        const std::u32string family = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                                   (llwchar)0x200D, (llwchar)0x1F467, (llwchar)'!' };
        ensure_equals("zwj family fwd", wstring_step_grapheme_forward(family, 0), size_t(5));
        ensure_equals("zwj family back", wstring_step_grapheme_backward(family, 5), size_t(0));

        // GB4 breaks after LF, which is also what bounds the backward scan.
        const std::u32string lines = { (llwchar)'a', (llwchar)'\n', (llwchar)'e', (llwchar)0x0301 };
        ensure_equals("break after lf", wstring_step_grapheme_backward(lines, 2), size_t(1));
        ensure_equals("cluster after lf", wstring_step_grapheme_backward(lines, 4), size_t(2));

        // Degenerate input: empty, and positions past the end clamp.
        const std::u32string empty;
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
        const std::u32string two_words = to_u32("foo bar");
        ensure_equals("fwd to next word", wstring_step_word_forward(two_words, 0), size_t(4));
        ensure_equals("fwd from next word", wstring_step_word_forward(two_words, 4), size_t(7));
        ensure_equals("back from end", wstring_step_word_backward(two_words, 7), size_t(4));
        ensure_equals("back over the space", wstring_step_word_backward(two_words, 4), size_t(0));
        ensure_equals("back from mid-word", wstring_step_word_backward(two_words, 5), size_t(4));

        // A full stop between letters does not split the word -- UAX #29 wants
        // example.com and 3.14 to hold together (WB6/WB7, FULL STOP is
        // MidNumLet). So the word here is "foo.bar" and the next one is "baz".
        const std::u32string dotted = to_u32("foo.bar baz");
        ensure_equals("dot does not split", wstring_step_word_forward(dotted, 0), size_t(8));
        ensure_equals("fwd from inside the dot word",
                      wstring_step_word_forward(dotted, 3), size_t(8));

        // A comma does separate, and standing on one used to wedge the cursor:
        // it is neither alnum nor a space, so both of the old loops declined to
        // move and the caller had to retry from pos+1 (LLLineEditor::removeWord
        // still carries that workaround).
        const std::u32string comma = to_u32("foo, bar");
        ensure_equals("fwd stops on the comma", wstring_step_word_forward(comma, 0), size_t(3));
        ensure_equals("fwd off the comma", wstring_step_word_forward(comma, 3), size_t(5));
        ensure("fwd never stands still", wstring_step_word_forward(comma, 3) > size_t(3));

        // An apostrophe is inside the word, not a break in it. The old walk
        // stopped at index 3, mid-word.
        const std::u32string contraction = to_u32("don't stop");
        ensure_equals("contraction is one word",
                      wstring_step_word_forward(contraction, 0), size_t(6));
        ensure_equals("back over a contraction",
                      wstring_step_word_backward(contraction, 10), size_t(6));

        // Tabs are whitespace to step over; the old test was == ' ' only.
        const std::u32string tabbed = to_u32("foo\tbar");
        ensure_equals("fwd over a tab", wstring_step_word_forward(tabbed, 0), size_t(4));

        // Neither direction crosses a newline, matching what the old walk did
        // by accident (a newline is neither a word char nor a space).
        const std::u32string lines = to_u32("ab\ncd");
        ensure_equals("fwd stops at eol", wstring_step_word_forward(lines, 0), size_t(2));
        ensure_equals("fwd parked on eol", wstring_step_word_forward(lines, 2), size_t(2));
        ensure_equals("back stops at bol", wstring_step_word_backward(lines, 5), size_t(3));
        ensure_equals("back parked on bol", wstring_step_word_backward(lines, 3), size_t(3));

        // Trailing whitespace has no word after it, so the line's end is where
        // forward stops.
        const std::u32string trailing = to_u32("foo   ");
        ensure_equals("fwd into trailing space",
                      wstring_step_word_forward(trailing, 0), size_t(6));

        // Degenerate input.
        const std::u32string empty;
        ensure_equals("empty fwd", wstring_step_word_forward(empty, 0), size_t(0));
        ensure_equals("empty back", wstring_step_word_backward(empty, 0), size_t(0));
        ensure_equals("past end fwd", wstring_step_word_forward(two_words, 99), two_words.size());
        ensure_equals("past end back", wstring_step_word_backward(two_words, 99), size_t(4));

        // Scripts without spaces still segment, rather than swallowing the
        // whole run the way an alnum test would.
        const std::u32string cjk = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E ok");
        ensure("cjk advances", wstring_step_word_forward(cjk, 0) > size_t(0));
        ensure("cjk terminates", wstring_step_word_forward(cjk, 0) <= cjk.size());

        // Adjacent emoji are separate words, so a word step crosses exactly
        // one. Callers must not pre-step a grapheme and then ask for the next
        // word -- that lands two emoji away.
        const std::u32string two_emoji = { (llwchar)0x1F436, (llwchar)0x1F431, (llwchar)'x' };
        ensure_equals("one emoji forward", wstring_step_word_forward(two_emoji, 0), size_t(1));
        ensure_equals("one emoji back", wstring_step_word_backward(two_emoji, 2), size_t(1));

        // A ZWJ sequence is a single word, however many codepoints it spans.
        const std::u32string flag_then_dog = { (llwchar)0x1F3F3, (llwchar)0xFE0F, (llwchar)0x200D,
                                          (llwchar)0x26A7, (llwchar)0xFE0F, (llwchar)0x1F436 };
        ensure_equals("zwj sequence is one word",
                      wstring_step_word_forward(flag_then_dog, 0), size_t(5));
        ensure_equals("back over the zwj sequence",
                      wstring_step_word_backward(flag_then_dog, 6), size_t(5));
    }

    // UAX #14 line break opportunities, which replaced a spaces-plus-CJK-range
    // heuristic in LLFontGL::maxDrawableBytes.
    template<> template<>
    void llstring_utf_object_t::test<119>()
    {
        std::vector<size_t> breaks;

        // A break is offered after the space, so the line keeps its trailing
        // space and the next one starts on the word. This is what the old
        // start_of_last_word tracking computed, and it has to stay that way.
        wstring_line_break_opportunities(to_u32("hello world"), breaks);
        ensure_equals("two opportunities", breaks.size(), size_t(2));
        ensure_equals("after the space", breaks[0], size_t(6));
        ensure_equals("and the end", breaks[1], size_t(11));

        // The string's end is always an opportunity; 0 never is.
        wstring_line_break_opportunities(to_u32("unbroken"), breaks);
        ensure_equals("single word, one opportunity", breaks.size(), size_t(1));
        ensure_equals("at the end", breaks[0], size_t(8));

        // A non-breaking space is glue. The old code special-cased U+00A0 by
        // hand; here it simply produces no opportunity.
        wstring_line_break_opportunities(to_u32("a\xC2\xA0" "b"), breaks);
        ensure_equals("nbsp does not break", breaks.size(), size_t(1));
        ensure_equals("only the end", breaks[0], size_t(3));

        // CJK ideographs may be split between characters...
        const std::u32string cjk = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        wstring_line_break_opportunities(cjk, breaks);
        ensure("cjk splits between characters", breaks.size() > size_t(1));

        // ...but a line may not begin with closing punctuation, so there is no
        // opportunity immediately before U+3002 IDEOGRAPHIC FULL STOP. The old
        // ideograph-range test would have broken there happily.
        const std::u32string cjk_stop = to_u32("\xE6\x97\xA5\xE6\x9C\xAC\xE3\x80\x82");
        wstring_line_break_opportunities(cjk_stop, breaks);
        ensure("no break before the full stop",
               std::find(breaks.begin(), breaks.end(), size_t(2)) == breaks.end());

        // Every opportunity is in range and strictly ascending, since the
        // consumer walks them with a single forward cursor.
        const std::u32string mixed = to_u32("one two\xC2\xA0three, four");
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
        wstring_line_break_opportunities(std::u32string(), breaks);
        ensure("empty clears", breaks.empty());
    }

    // Case conversion, and the index map that lets a search fold its haystack
    // without losing track of where a match sits in the original.
    template<> template<>
    void llstring_utf_object_t::test<120>()
    {
        // Uppercasing sharp s produces two characters. towupper could not do
        // this at all -- it maps one codepoint to one codepoint.
        std::string sharp_s = "stra\xC3\x9F" "e";
        ensure_equals("sharp s starts at 7 bytes", sharp_s.size(), size_t(7));
        LLStringUtil::toUpper(sharp_s);
        ensure_equals("uppercased grows", sharp_s, std::string("STRASSE"));

        std::string plain = "Hello";
        LLStringUtil::toLower(plain);
        ensure_equals("ordinary lowercase", plain, std::string("hello"));

        // U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE lowercases to two
        // characters, so every offset after it shifts. This is the case that
        // breaks an offset taken from a folded copy.
        const std::string dotted_i = "a\xC4\xB0" "b";
        ensure_equals("four bytes in", dotted_i.size(), size_t(4));

        std::string folded = dotted_i;
        LLStringUtil::toLower(folded);
        ensure("fold grew", folded.size() > dotted_i.size());

        // The point of all this: find 'b' in the folded copy and land on the
        // right byte in the original. Taken raw, the offset would be past it.
        const size_t folded_at = folded.find('b');
        ensure("found in fold", folded_at != std::string::npos);
        ensure("raw offset would be wrong", folded_at != size_t(3));
        ensure_equals("mapped offset is right",
                      utf8str_bytes_from_cased_bytes(dotted_i, folded_at, false), size_t(3));

        // Every prefix of the fold maps back onto a character boundary of the
        // original, and never past its end.
        for (size_t k = 0; k <= folded.size(); ++k)
        {
            const size_t back = utf8str_bytes_from_cased_bytes(dotted_i, k, false);
            ensure("never past the end", back <= dotted_i.size());
            ensure("lands on a character start",
                   back == dotted_i.size()
                   || (((unsigned char)dotted_i[back]) & 0xC0) != 0x80);
        }

        // ASCII stays one-to-one, so an offset needs no mapping at all.
        const std::string ascii = "ABC";
        for (size_t k = 0; k <= ascii.size(); ++k)
        {
            ensure_equals("ascii maps to itself",
                          utf8str_bytes_from_cased_bytes(ascii, k, false), k);
        }

        // Sharp s changes the character count without changing the byte
        // count: two bytes of ß become two bytes of SS. The totals matching is
        // what makes it a trap -- offset 5 sits between the two S's, which is
        // no position in the original at all, and the answer backs off to
        // before the ß rather than inventing one.
        const std::string strasse = "stra\xC3\x9F" "e";
        std::string upper = strasse;
        LLStringUtil::toUpper(upper);
        ensure_equals("same byte length", upper.size(), strasse.size());
        ensure_equals("but one more character",
                      utf8str_codepoint_count(upper), utf8str_codepoint_count(strasse) + 1);

        const size_t back_from[] = { 0, 1, 2, 3, 4, 4, 6, 7 };
        for (size_t k = 0; k <= upper.size(); ++k)
        {
            ensure_equals("cased offset " + std::to_string(k) + " maps back",
                          utf8str_bytes_from_cased_bytes(strasse, k, true), back_from[k]);
        }
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

        // The ff ligature uppercases to "FF": three bytes become two, so an
        // offset taken from the key sits BEFORE where the same text starts in
        // the label. This is the case the mapping exists for.
        {
            const std::string label = "a\xEF\xAC\x80" "b xyz";
            std::string key = label;
            LLStringUtil::toUpper(key);
            const size_t at = key.find("XYZ");
            ensure("found in the key", at != std::string::npos);
            ensure("the ligature expanded, so the key is shorter",
                   key.size() < label.size());

            // xyz begins at byte 6 of the label and byte 5 of the key.
            ensure_equals("offset maps onto the label's own bytes",
                          utf8str_bytes_from_cased_bytes(label, at, true), size_t(6));
            ensure("the raw offset would have been wrong", at != size_t(6));
        }

        // "straße" uppercases to "STRASSE": one codepoint longer, but the same
        // number of bytes, because the sharp s already took two. In bytes the
        // offset maps straight through -- which is the point of counting them.
        const std::string label = "stra\xC3\x9F" "e xyz";
        std::string key = label;
        LLStringUtil::toUpper(key);
        const size_t at = key.find("XYZ");
        ensure("found in the key", at != std::string::npos);
        ensure_equals("same byte length either cased or not", key.size(), label.size());

        const size_t mapped = utf8str_bytes_from_cased_bytes(label, at, true);
        ensure_equals("mapped to the label's own byte offset", mapped, size_t(8));

        // And the matched span's own length, which is not the key's.
        const size_t span_begin = utf8str_bytes_from_cased_bytes(label, 0, true);
        const size_t span_end = utf8str_bytes_from_cased_bytes(label, key.find(' '), true);
        ensure_equals("word before the space is seven bytes", span_end - span_begin, size_t(7));

        // Offset 0 and a whole-string offset are the degenerate ends.
        ensure_equals("zero maps to zero",
                      utf8str_bytes_from_cased_bytes(label, 0, true), size_t(0));
        ensure_equals("whole string maps to its byte count",
                      utf8str_bytes_from_cased_bytes(label, key.size(), true), size_t(11));

        // ASCII text maps one-to-one, so existing offsets are unaffected.
        const std::string plain = "hello world";
        ensure_equals("ascii maps straight through",
                      utf8str_bytes_from_cased_bytes(plain, 6, true), size_t(6));

        ensure_equals("empty", utf8str_bytes_from_cased_bytes(std::string(), 4, true), size_t(0));
    }

    // "Which word is here" and "where is the next one", the questions
    // double-click selection, spell check and autoreplace ask.
    template<> template<>
    void llstring_utf_object_t::test<122>()
    {
        const std::u32string text = to_u32("don't stop, ok");

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
            found.push_back(to_utf8(text.substr(next.first, next.second - next.first)));
            at = next.second;
        }
        ensure_equals("three words", found.size(), size_t(3));
        ensure_equals("first", found[0], std::string("don't"));
        ensure_equals("second", found[1], std::string("stop"));
        ensure_equals("third", found[2], std::string("ok"));

        // Iteration crosses lines, which the spell checker relies on.
        const std::u32string lines = to_u32("one\ntwo");
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

        const std::u32string empty;
        const auto on_empty = wstring_word_range_at(empty, 0);
        ensure("empty string", on_empty.first == on_empty.second);

        // A whole string that is one word -- what autoreplace validates a
        // keyword with.
        const std::u32string keyword = to_u32("don't");
        const auto whole = wstring_word_range_at(keyword, 0);
        ensure("keyword is one word",
               whole.first == size_t(0) && whole.second == keyword.size());
        const std::u32string two_words = to_u32("not one");
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
        std::u32string text = to_u32("ab");
        text.push_back(0x2000A);
        text.push_back(0x2000A);
        text += to_u32(" cd");

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
        // a=0 b=1 sp=2 CJK=3,6 sp=9 e=10 acute=11 sp=13 flag=14 sp=22 end=23.
        ensure_equals("layout", utf8.size(), size_t(26));

        // Every grapheme boundary, written out. The combining acute and the
        // two regional indicators are the ones a byte walk would split.
        const size_t boundaries[] = { 0, 1, 2, 3, 6, 9, 10, 13, 14, 22, 23, 24, 25, 26 };
        const size_t count = sizeof(boundaries) / sizeof(boundaries[0]);

        size_t at = 0;
        for (size_t i = 1; i < count; ++i)
        {
            at = utf8str_step_grapheme_forward(utf8, at);
            ensure_equals("forward boundary " + std::to_string(i), at, boundaries[i]);
        }
        ensure_equals("forward walk ended at the end", at, utf8.size());

        for (size_t i = count - 1; i > 0; --i)
        {
            at = utf8str_step_grapheme_backward(utf8, at);
            ensure_equals("backward boundary " + std::to_string(i - 1), at, boundaries[i - 1]);
        }
        ensure_equals("backward walk came back to nothing", at, size_t(0));

        // Every offset inside a cluster snaps to that cluster's edges.
        for (size_t i = 1; i < count; ++i)
        {
            for (size_t inside = boundaries[i - 1] + 1; inside < boundaries[i]; ++inside)
            {
                ensure_equals("aligns back", utf8str_grapheme_align_backward(utf8, inside),
                              boundaries[i - 1]);
                ensure_equals("aligns forward", utf8str_grapheme_align_forward(utf8, inside),
                              boundaries[i]);
            }
        }

        // Word stepping lands on the start of each word, never in the gaps.
        const size_t word_starts[] = { 3, 10, 14, 23, 26 };
        size_t word_at = 0;
        for (size_t i = 0; i < sizeof(word_starts) / sizeof(word_starts[0]); ++i)
        {
            word_at = utf8str_step_word_forward(utf8, word_at);
            ensure_equals("word start " + std::to_string(i), word_at, word_starts[i]);
        }

        // The word under the first ideograph is the ideograph pair.
        const auto cjk_word = utf8str_word_range_at(utf8, 3);
        ensure_equals("CJK word begins", cjk_word.first,  size_t(3));
        ensure_equals("CJK word ends",   cjk_word.second, size_t(9));

        // Line break opportunities: after each space, between the ideographs,
        // and at the end. Never inside a character or a flag.
        std::vector<size_t> breaks;
        utf8str_line_break_opportunities(utf8, breaks);
        ensure("the end is always an opportunity",
               !breaks.empty() && breaks.back() == utf8.size());
        for (size_t where : breaks)
        {
            ensure("never zero", where != 0);
            ensure("always on a grapheme boundary",
                   std::find(boundaries, boundaries + count, where) != boundaries + count);
        }
        ensure("CJK may be split between the ideographs",
               std::find(breaks.begin(), breaks.end(), size_t(6)) != breaks.end());
        ensure("a flag may not be split",
               std::find(breaks.begin(), breaks.end(), size_t(18)) == breaks.end());

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

    // ---------------------------------------------------------------
    //                 12x  utf8 walkers
    // ---------------------------------------------------------------

    // The two cluster walkers are one set of rules instantiated twice, so
    // every run the wide walker finds has to be the same run in the UTF-8
    // walker's byte coordinates, and there must be no extra ones.
    template<> template<>
    void llstring_utf_object_t::test<127>()
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
        ensure_equals("every corpus entry is accounted for",
                      corpus.size(), sizeof(expected) / sizeof(expected[0]));

        for (size_t c = 0; c < corpus.size(); ++c)
        {
            const std::string utf8 = to_utf8(corpus[c]);
            const std::string tag  = "corpus " + std::to_string(c);
            ensure_equals(tag + ": byte length", utf8.size(), expected[c].bytes);

            const auto runs = utf8str_find_emoji_clusters(utf8);
            ensure_equals(tag + ": run count", runs.size(), expected[c].has_run ? size_t(1) : size_t(0));
            if (expected[c].has_run)
            {
                ensure_equals(tag + ": run begin", runs[0].first,  expected[c].begin);
                ensure_equals(tag + ": run end",   runs[0].second, expected[c].end);
            }
        }
    }

    // Byte coordinates pinned outright, so a change that moved both walkers
    // the same wrong way is still caught. A ZWJ pair sitting behind an
    // ideograph starts at byte 4, not codepoint 2.
    template<> template<>
    void llstring_utf_object_t::test<128>()
    {
        std::u32string wide = { (llwchar)'a', (llwchar)0x65E5,
                           (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                           (llwchar)'z' };
        const std::string utf8 = to_utf8(wide);
        ensure_equals("byte length", utf8.size(), size_t(1 + 3 + 4 + 3 + 4 + 1));

        const auto runs = utf8str_find_emoji_clusters(utf8);
        ensure_equals("one run", runs.size(), size_t(1));
        ensure_equals("begins past 'a' and the ideograph", runs[0].first,  size_t(4));
        ensure_equals("ends before 'z'",                   runs[0].second, size_t(15));

    }

    // Malformed UTF-8 must not stall the walk or invent a cluster. Every bad
    // byte decodes to U+FFFD over one byte, which matches no rule.
    template<> template<>
    void llstring_utf_object_t::test<129>()
    {
        ensure_equals("lone continuation",
                      utf8str_find_emoji_clusters("\x80\x80\x80").size(), size_t(0));
        ensure_equals("truncated four-byte",
                      utf8str_find_emoji_clusters("\xF0\x9F\x91").size(), size_t(0));
        ensure_equals("bad lead",
                      utf8str_find_emoji_clusters("\xFF\xFE").size(),     size_t(0));

        // A bad byte in front of a real cluster still leaves the cluster
        // findable, at the offset that byte pushed it to.
        std::string text = "\xFF";
        text += to_utf8(std::u32string{ (llwchar)0x1F468, (llwchar)0x200D,
                                              (llwchar)0x1F469 });
        const auto runs = utf8str_find_emoji_clusters(text);
        ensure_equals("one run past the bad byte", runs.size(),     size_t(1));
        ensure_equals("begins at byte 1",          runs[0].first,   size_t(1));
        ensure_equals("ends at the end",           runs[0].second,  text.size());
    }

    // utf8str_emoji_range_at answered at every position in the string --
    // including the ones inside a character, which belong to no pictograph.
    // The expected ranges are written out rather than derived, so a change that
    // moved the lookup and the walk the same wrong way is still caught.
    template<> template<>
    void llstring_utf_object_t::test<130>()
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
        ensure_equals("layout", utf8.size(), size_t(24));

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
            ensure_equals("range begin at " + std::to_string(e.at), got.first,  e.begin);
            ensure_equals("range end at "   + std::to_string(e.at), got.second, e.end);
        }

        // The heart carries a presentation selector, so it is a cluster and
        // its range spans both codepoints -- three bytes plus three.
        const auto heart = utf8str_emoji_range_at(utf8, 1);
        ensure_equals("heart begins at 1", heart.first,  size_t(1));
        ensure_equals("heart spans VS16",  heart.second, size_t(7));

        // The lone copyright sign is no cluster, but it is a pictograph base,
        // so it reports its own two bytes rather than an empty range.
        const size_t copy_at = 1 + 6 + 3 + 11;
        const auto copyright = utf8str_emoji_range_at(utf8, copy_at);
        ensure_equals("(c) begins", copyright.first,  copy_at);
        ensure_equals("(c) is two bytes", copyright.second, copy_at + 2);

        // A position inside the ideograph is not a character start and so
        // belongs to nothing.
        const auto split = utf8str_emoji_range_at(utf8, 8);
        ensure_equals("mid-character is empty", split.first, split.second);

        // Past the end reports an empty range at the position asked about,
        // not at the end of the string.
        const auto past = utf8str_emoji_range_at(utf8, utf8.size() + 4);
        ensure_equals("past the end is empty", past.first, past.second);
    }

    // utf8str_codepoint_count is what a limit expressed in characters gets
    // compared against, so it has to agree with the UTF-32 form's size().
    template<> template<>
    void llstring_utf_object_t::test<132>()
    {
        ensure_equals("empty", utf8str_codepoint_count(""), size_t(0));
        ensure_equals("ascii", utf8str_codepoint_count("abc"), size_t(3));

        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)0x200D, (llwchar)0x1F600 };
        const std::string utf8 = to_utf8(wide);
        ensure_equals("bytes", utf8.size(), size_t(1 + 2 + 3 + 4 + 3 + 4));
        ensure_equals("agrees with the wide size",
                      utf8str_codepoint_count(utf8), wide.size());

        // Malformed bytes count one each, so the count never disagrees with a
        // walk over the same text.
        ensure_equals("lone continuation bytes",
                      utf8str_codepoint_count("\x80\x80"), size_t(2));
        ensure_equals("truncated sequence",
                      utf8str_codepoint_count("\xF0\x9F"), size_t(2));
    }

    // The UTF-16 pair has to agree with the wide forms it stands beside: the
    // Win32 IME hands offsets in code units and the editors index bytes, so a
    // disagreement here is a caret landing in the wrong place mid-composition.
    template<> template<>
    void llstring_utf_object_t::test<133>()
    {
        // 'a', e-acute, an ideograph, an astral emoji (a surrogate pair), 'z'.
        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)'z' };
        const std::string utf8 = to_utf8(wide);

        // Bytes 1 + 2 + 3 + 4 + 1; code units 1 + 1 + 1 + 2 + 1.
        ensure_equals("bytes", utf8.size(), size_t(11));
        ensure_equals("units", utf8str_utf16_length(utf8, 0, S32_MAX), S32(6));

        // Every prefix, by hand: the byte offset of each character start and
        // the code units the text up to it occupies.
        const S32 byte_at[] = { 0, 1, 3, 6, 10, 11 };
        const S32 unit_at[] = { 0, 1, 2, 3,  5,  6 };
        for (size_t cp = 0; cp < 6; ++cp)
        {
            ensure_equals("prefix at " + std::to_string(cp),
                          utf8str_utf16_length(utf8, 0, byte_at[cp]), unit_at[cp]);
        }

        // Back the other way: a budget in code units becomes a byte count, and
        // a budget that stops between the emoji's surrogate halves says so.
        const S32 bytes_for_units[] = { 0, 1, 3, 6, 6, 10, 11 };
        for (S32 units = 0; units <= 6; ++units)
        {
            bool unaligned = false;
            ensure_equals("bytes for " + std::to_string(units) + " units",
                          utf8str_length_from_utf16_length(utf8, 0, units, &unaligned),
                          bytes_for_units[units]);
            ensure("unaligned only mid-pair", unaligned == (units == 4));
        }

        // Offsets are honoured, not just whole-string calls.
        ensure_equals("offset form", utf8str_utf16_length(utf8, 3, S32_MAX), S32(4));

        // Out of range clamps rather than running off.
        ensure_equals("past the end", utf8str_utf16_length(utf8, 999, 999), S32(0));
        ensure_equals("no budget", utf8str_length_from_utf16_length(utf8, 0, 0), S32(0));
    }

    // Valid text has to come back byte-identical -- the clipboard calls this on
    // every paste, and a sanitizer that rewrites what was already correct would
    // be worse than none.
    template<> template<>
    void llstring_utf_object_t::test<134>()
    {
        const std::string ascii = "plain ascii";
        ensure_equals("ascii untouched", utf8str_sanitize(ascii), ascii);

        const std::u32string wide = { (llwchar)'a', (llwchar)0x00E9, (llwchar)0x65E5,
                                 (llwchar)0x1F600, (llwchar)'z' };
        const std::string mixed = to_utf8(wide);
        ensure_equals("mixed untouched", utf8str_sanitize(mixed), mixed);

        ensure_equals("empty", utf8str_sanitize(std::string_view()), std::string());

        // Embedded NUL is valid UTF-8 and must survive; string_view carries it.
        const std::string with_nul("a\0b", 3);
        ensure_equals("nul survives", utf8str_sanitize(with_nul), with_nul);
    }

    // And malformed text has to come back valid, whatever shape the damage took.
    template<> template<>
    void llstring_utf_object_t::test<135>()
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
            ensure("input really is malformed", !is_valid(s));
            // The predicate the widgets ask before deciding whether to repair
            // has to agree with simdutf, or a widget skips a repair it needed.
            ensure_equals("utf8str_is_valid agrees with simdutf",
                          utf8str_is_valid(s), is_valid(s));
            const std::string fixed = utf8str_sanitize(s);
            ensure("output is valid", is_valid(fixed));
            ensure_equals("and the predicate says so too",
                          utf8str_is_valid(fixed), true);
            ensure("the good bytes are still there",
                   fixed.find("ok") == 0 && fixed.rfind("ok") == fixed.size() - 2);
        }
    }

    // A decoder that accepts more than the encoding allows lets a character
    // reach somewhere a filter already looked for it.
    template<> template<>
    void llstring_utf_object_t::test<136>()
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
            ensure_equals(std::string(c.what) + " decodes to the replacement",
                          (S32)at.cp, (S32)0xFFFD);
            ensure_equals(std::string(c.what) + " consumes exactly one byte",
                          (S32)at.next, 1);
        }

        // The shapes that are legal still decode whole.
        ensure_equals("ASCII", (S32)utf8str_decode_at(std::string("A"), 0).cp, (S32)'A');
        ensure_equals("two-byte", (S32)utf8str_decode_at(std::string("\xC3\xA9"), 0).cp, (S32)0xE9);
        ensure_equals("three-byte", (S32)utf8str_decode_at(std::string("\xE6\x97\xA5"), 0).cp, (S32)0x65E5);
        ensure_equals("four-byte", (S32)utf8str_decode_at(std::string("\xF0\x9F\x90\xB6"), 0).cp, (S32)0x1F436);
        ensure_equals("four-byte spans four", (S32)utf8str_decode_at(std::string("\xF0\x9F\x90\xB6"), 0).next, (S32)4);
    }

    // Shortening text must not empty it. The budget counts codepoints while the
    // cut lands on a cluster, so a first cluster that spends more codepoints
    // than the budget allows has nowhere to fall back to except itself.
    template<> template<>
    void llstring_utf_object_t::test<137>()
    {
        // A ZWJ family: five codepoints, eighteen bytes, one cluster.
        const std::string family("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
        const std::string text = family + "abc";

        for (S32 budget = 1; budget <= 5; ++budget)
        {
            const std::string cut = utf8str_symbol_truncate(text, budget);
            ensure("a budget under the first cluster still returns text", !cut.empty());
            ensure_equals("and returns the whole of that cluster", cut, family);
        }

        // Once the budget clears the cluster the next characters come too.
        ensure_equals("budget past the cluster takes the next character",
                      utf8str_symbol_truncate(text, 6), family + "a");
        ensure_equals("input already short enough is untouched",
                      utf8str_symbol_truncate(std::string("abc"), 10), std::string("abc"));
    }

    // Case-insensitive over ASCII compares bytes, so a spelling the format
    // never allowed cannot pass for the one it did.
    template<> template<>
    void llstring_utf_object_t::test<138>()
    {
        ensure("same word, different case",
               LLStringUtil::isEqualInsensitiveASCII(std::string("llsd/binary"), std::string("LLSD/Binary")));
        ensure("a different length never matches",
               !LLStringUtil::isEqualInsensitiveASCII(std::string("llsd"), std::string("llsd/binary")));
        // Fullwidth 'l' is its own character, whatever a collator makes of it.
        ensure("fullwidth is not the ASCII letter",
               !LLStringUtil::isEqualInsensitiveASCII(
                   std::string("\xEF\xBD\x8C\xEF\xBD\x8Csd/binary"), std::string("llsd/binary")));
        // A soft hyphen is a character, not an absence of one.
        ensure("an ignorable is still a difference",
               !LLStringUtil::isEqualInsensitiveASCII(
                   std::string("llsd/bin\xC2\xAD" "ary"), std::string("llsd/binary")));
        // Above ASCII nothing is folded -- these are simply different bytes.
        ensure("no folding above ASCII",
               !LLStringUtil::isEqualInsensitiveASCII(
                   std::string("caf\xC3\xA9"), std::string("caf\xC3\x89")));
        ensure("empty equals empty",
               LLStringUtil::isEqualInsensitiveASCII(std::string(), std::string()));
    }

    // The word walkers driven by their own offsets. The tests beside these go
    // through the wstring_* adapters, which prove the ALUtf8View index map is
    // self-consistent -- not that a byte offset supplied by a caller is read
    // correctly, which is what every caller in llui now passes.
    template<> template<>
    void llstring_utf_object_t::test<139>()
    {
        // ASCII first, so the byte answers can be read against the wide test.
        const std::string two_words = "foo bar";
        ensure_equals("fwd to next word", utf8str_step_word_forward(two_words, 0), size_t(4));
        ensure_equals("fwd from next word", utf8str_step_word_forward(two_words, 4), size_t(7));
        ensure_equals("back from end", utf8str_step_word_backward(two_words, 7), size_t(4));
        ensure_equals("back over the space", utf8str_step_word_backward(two_words, 4), size_t(0));

        // And where a character is not a byte. "café bar": the accent makes
        // the second word start at byte 6 where it is codepoint 5, so these
        // numbers are wrong under any reading but the byte one.
        const std::string accented = "caf\xC3\xA9 bar";
        ensure_equals("the fixture string is nine bytes",
                      accented.size(), size_t(9));
        ensure_equals("fwd over an accented word",
                      utf8str_step_word_forward(accented, 0), size_t(6));
        ensure_equals("fwd from the last word runs to the end",
                      utf8str_step_word_forward(accented, 6), size_t(9));
        ensure_equals("back from the end",
                      utf8str_step_word_backward(accented, 9), size_t(6));
        ensure_equals("back over the space",
                      utf8str_step_word_backward(accented, 6), size_t(0));

        // An offset inside the accent is still inside the first word.
        ensure_equals("stepping from inside a character",
                      utf8str_step_word_forward(accented, 4), size_t(6));

        // The walkers stay on their line -- LLTextEditor::nextWordPos crosses
        // the break itself, and would stall on every newline if this changed.
        const std::string lines = "ab\ncd";
        ensure_equals("fwd stops at eol", utf8str_step_word_forward(lines, 0), size_t(2));
        ensure_equals("fwd parked on eol", utf8str_step_word_forward(lines, 2), size_t(2));
        ensure_equals("back parked on bol", utf8str_step_word_backward(lines, 3), size_t(3));
    }

    // Word ranges in bytes: what a double-click selects, and what comes next.
    template<> template<>
    void llstring_utf_object_t::test<140>()
    {
        const std::string accented = "caf\xC3\xA9 bar";

        // Anywhere inside the first word reports the whole of it, including a
        // position inside the accent.
        for (size_t at : { size_t(0), size_t(2), size_t(3), size_t(4) })
        {
            const auto range = utf8str_word_range_at(accented, at);
            ensure_equals("word range begins at 0", range.first, size_t(0));
            ensure_equals("word range ends after the accent", range.second, size_t(5));
        }

        const auto second = utf8str_word_range_at(accented, 7);
        ensure_equals("second word begins at 6", second.first, size_t(6));
        ensure_equals("second word ends at 9", second.second, size_t(9));

        // next_word_range finds the word whose end lies past the position --
        // the one the position is in, when it is in one, and only otherwise
        // the following word. From inside the first word that is still the
        // first word.
        const auto from_start = utf8str_next_word_range(accented, 0);
        ensure_equals("from 0 the word is the one at 0", from_start.first, size_t(0));
        ensure_equals("and it ends after the accent", from_start.second, size_t(5));

        // Standing on the space, the answer is the word after it.
        const auto from_space = utf8str_next_word_range(accented, 5);
        ensure_equals("from the space the next word begins at 6",
                      from_space.first, size_t(6));
        ensure_equals("and ends at 9", from_space.second, size_t(9));

        // Past the last word there is nothing further to select.
        const auto none = utf8str_next_word_range(accented, accented.size());
        ensure_equals("nothing begins past the end", none.first, accented.size());
        ensure_equals("nothing ends past the end", none.second, accented.size());
    }

    // The caret forms differ from the raw walkers in exactly one way: they
    // cross a line break instead of stalling on it. Both text widgets forward
    // to these rather than carrying the policy twice, so it is worth pinning
    // the difference rather than only the agreement.
    template<> template<>
    void llstring_utf_object_t::test<141>()
    {
        const std::string lines = "one two\nthree four";

        // Away from a break the two are the same walk.
        ensure_equals("forward agrees mid-line",
                      utf8str_caret_word_forward(lines, 0),
                      utf8str_step_word_forward(lines, 0));
        ensure_equals("backward agrees mid-line",
                      utf8str_caret_word_backward(lines, 7),
                      utf8str_step_word_backward(lines, 7));

        // A caret already at the edge of a word must not skip the adjacent
        // word, and a caret inside a word must land at that word's start.
        const std::string adjacent = "foo bar";
        ensure_equals("forward from a word end reaches the adjacent word",
                      utf8str_caret_word_forward(adjacent, 3), size_t(4));
        ensure_equals("backward from inside a word reaches its start",
                      utf8str_caret_word_backward(adjacent, 5), size_t(4));

        // On the break the raw walker has nowhere to go and says so.
        ensure_equals("the raw walker stalls on the newline",
                      utf8str_step_word_forward(lines, 7), size_t(7));
        ensure("the caret walker crosses it",
               utf8str_caret_word_forward(lines, 7) > size_t(7));

        // And at a line start, going back.
        ensure_equals("the raw walker stalls at the line start",
                      utf8str_step_word_backward(lines, 8), size_t(8));
        ensure("the caret walker crosses back",
               utf8str_caret_word_backward(lines, 8) < size_t(8));

        // Neither runs off either end.
        ensure_equals("nothing before the beginning",
                      utf8str_caret_word_backward(lines, 0), size_t(0));
        ensure_equals("nothing after the end",
                      utf8str_caret_word_forward(lines, lines.size()), lines.size());

        // Repeated stepping terminates rather than parking.
        size_t at = 0;
        for (int i = 0; i < 32 && at < lines.size(); ++i)
        {
            const size_t next = utf8str_caret_word_forward(lines, at);
            ensure("forward never moves backward", next >= at);
            if (next == at) break;
            at = next;
        }
        ensure_equals("stepping forward reaches the end", at, lines.size());
    }

    // ---------------------------------------------------------------
    //                              trim
    // ---------------------------------------------------------------

    // LLStringUtil::trim answers for Unicode whitespace, not just ASCII. A
    // no-break space arrives pasted out of a web page and an ideographic
    // space sits either side of CJK; a byte-wise isspace() saw neither.
    template<> template<>
    void llstring_utf_object_t::test<142>()
    {
        const std::string nbsp   = "\xC2\xA0";      // U+00A0
        const std::string ideo   = "\xE3\x80\x80";  // U+3000
        const std::string em     = "\xE2\x80\x83";  // U+2003
        const std::string narrow = "\xE2\x80\xAF";  // U+202F
        const std::string cjk    = "\xE6\x97\xA5\xE6\x9C\xAC";

        std::string s = nbsp + "Rye" + nbsp;
        LLStringUtil::trim(s);
        ensure_equals("a no-break space is whitespace", s, std::string("Rye"));

        s = ideo + cjk + ideo;
        LLStringUtil::trim(s);
        ensure_equals("an ideographic space is whitespace", s, cjk);

        s = em + narrow + " \tx\t " + narrow + em;
        LLStringUtil::trim(s);
        ensure_equals("ASCII and Unicode spaces mixed", s, std::string("x"));

        s = nbsp + "a";
        LLStringUtil::trimHead(s);
        ensure_equals("trimHead crosses one", s, std::string("a"));

        s = "a" + nbsp;
        LLStringUtil::trimTail(s);
        ensure_equals("trimTail crosses one", s, std::string("a"));

        s = nbsp + ideo + em;
        LLStringUtil::trim(s);
        ensure("all whitespace trims to nothing", s.empty());
    }

    // What is not White_Space stays, and malformed bytes are content: a trim
    // that guessed at them could cut a character in half.
    template<> template<>
    void llstring_utf_object_t::test<143>()
    {
        const std::string zwsp = "\xE2\x80\x8B";  // U+200B, not White_Space
        const std::string bom  = "\xEF\xBB\xBF";  // U+FEFF, not White_Space

        std::string s = zwsp + "a" + zwsp;
        LLStringUtil::trim(s);
        ensure_equals("a zero-width space is not whitespace", s, zwsp + "a" + zwsp);

        s = bom + "a" + bom;
        LLStringUtil::trim(s);
        ensure_equals("a byte order mark is not whitespace", s, bom + "a" + bom);

        s = std::string("\x80") + "a" + std::string("\x80");
        LLStringUtil::trim(s);
        ensure_equals("a stray continuation byte is content", s.size(), size_t(3));

        // U+3000 missing its last byte. The lead byte it shares with a real
        // ideographic space must not be enough to cut it.
        s = std::string("a") + "\xE3\x80";
        LLStringUtil::trim(s);
        ensure_equals("a truncated character is left whole", s.size(), size_t(3));

        s = "  hello  ";
        LLStringUtil::trim(s);
        ensure_equals("ASCII trims as it always did", s, std::string("hello"));

        s.clear();
        LLStringUtil::trim(s);
        ensure("an empty string survives", s.empty());
    }

    // The view forms cut where the string forms cut, and utf8str_trim shares
    // that decision rather than keeping a second one.
    template<> template<>
    void llstring_utf_object_t::test<144>()
    {
        const std::string src = std::string("\xC2\xA0") + "\t" + "caf\xC3\xA9" + "\xE3\x80\x80";
        const std::string expected = "caf\xC3\xA9";

        std::string owned = src;
        LLStringUtil::trim(owned);
        ensure_equals("the string form", owned, expected);

        std::string_view view = src;
        LLStringUtil::trim(view);
        ensure_equals("the view form cuts the same place", std::string(view), expected);

        ensure_equals("utf8str_trim agrees", utf8str_trim(src), expected);
    }
}
