/**
 * @file llzerocode_test.cpp
 * @brief LLZeroCode test cases.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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

#include "../llzerocode.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    // Builds header_size bytes of header (with header[0] == header0) followed by body.
    static std::vector<U8> makeBuffer(U32 header_size, U8 header0, const std::vector<U8>& body)
    {
        std::vector<U8> buf(header_size, 0);
        if (header_size)
        {
            buf[0] = header0;
        }
        buf.insert(buf.end(), body.begin(), body.end());
        return buf;
    }

    // Runs src through encode() then decode(), and (when compression is expected to help)
    // asserts the round trip reproduces src exactly, including untouched header bits other
    // than the zero-code flag. When compression is not expected to help, asserts encode()
    // refuses it.
    static void require_round_trip(const char* what, U32 header_size, U8 header0,
                                   const std::vector<U8>& body, bool expect_compressed)
    {
        INFO(what);
        std::vector<U8> src = makeBuffer(header_size, header0, body);
        std::vector<U8> enc(2 * src.size() + 16, 0xAA);

        S32 enc_size = LLZeroCode::encode(src.data(), (U32)src.size(),
                                           enc.data(), (U32)enc.size(), header_size);

        if (!expect_compressed)
        {
            UNSCOPED_INFO("encode refuses: no benefit");
            REQUIRE(enc_size < 0);
            return;
        }

        REQUIRE(enc_size >= 0);
        REQUIRE((U32)enc_size < src.size());
        REQUIRE((enc[0] & LLZeroCode::FLAG) != 0);
        if (header_size > 0)
        {
            UNSCOPED_INFO("encode keeps the header bits other than FLAG");
            REQUIRE((U8)(enc[0] & ~LLZeroCode::FLAG) == (U8)(header0 & ~LLZeroCode::FLAG));
        }

        std::vector<U8> dec(src.size() + 16, 0xBB);
        bool overflow = false;
        U32 dec_size = LLZeroCode::decode(enc.data(), (U32)enc_size,
                                           dec.data(), (U32)dec.size(), header_size, overflow);
        REQUIRE_FALSE(overflow);
        REQUIRE(dec_size == (U32)src.size());
        REQUIRE(memcmp(dec.data(), src.data(), src.size()) == 0);
    }

    // Basic mixed body; header carries an unrelated flag bit (0x40) that must survive untouched.
    TEST_CASE("LLZeroCode: a mixed body round-trips and keeps other header bits", "[LLZeroCode]")
    {
        require_round_trip("mixed body", 6, 0x40,
                           {0,0,0,5,0,0,0,0,0,0,7,8,9,0,0}, true);
    }

    // A body with no zero bytes at all cannot benefit from zero-coding.
    TEST_CASE("LLZeroCode: encode refuses a body with no zero bytes", "[LLZeroCode]")
    {
        require_round_trip("no zero bytes", 1, 0x00, {1,2,3,4,5,6,7,8,9}, false);
    }

    // Isolated zero-byte runs of length 1 or 2 cost as much or more than they save
    // (marker + terminator == 2 bytes), so encode must refuse them.
    TEST_CASE("LLZeroCode: encode refuses isolated runs of one or two zeros", "[LLZeroCode]")
    {
        require_round_trip("isolated single zero", 1, 0x00, {5, 0}, false);
        require_round_trip("isolated double zero", 1, 0x00, {5, 0, 0, 9}, false);
    }

    // A long enough zero run followed by other data pays off.
    TEST_CASE("LLZeroCode: a long zero run followed by data round-trips smaller", "[LLZeroCode]")
    {
        std::vector<U8> body(20, 0);
        body.push_back(99);
        require_round_trip("long zero run", 1, 0x00, body, true);
    }

    // Wrap boundary: runs are split into chunks of at most 255 zero bytes each
    // (encode never emits the doubled-0x00 wire form; see test<7> for that).
    // A run only shrinks the packet once it is longer than 2 bytes.
    TEST_CASE("LLZeroCode: runs around the 255-byte wrap round-trip before a trailing byte", "[LLZeroCode]")
    {
        static const U32 lengths[] = {1, 2, 253, 254, 255, 256, 257, 300, 509, 510, 511, 1000};
        for (U32 n : lengths)
        {
            CAPTURE(n);
            std::vector<U8> body(n, 0);
            body.push_back(42); // trailing nonzero byte
            require_round_trip("wrap boundary (with trailing byte)", 1, 0x00, body, n > 2);
        }
    }

    // Same boundary check, but with the zero run flushed at end-of-buffer
    // (no trailing nonzero byte to force the terminator write mid-loop).
    TEST_CASE("LLZeroCode: runs around the 255-byte wrap round-trip at the end of the buffer", "[LLZeroCode]")
    {
        static const U32 lengths[] = {1, 2, 254, 255, 256, 300};
        for (U32 n : lengths)
        {
            CAPTURE(n);
            std::vector<U8> body(n, 0);
            require_round_trip("wrap boundary (end-of-buffer flush)", 1, 0x00, body, n > 2);
        }
    }

    // decode() must also accept the "0x00 0x00 N" doubled-marker wire format
    // (worth +255 zero bytes per extra marker) for compatibility with any
    // encoder other than this one's chunking strategy, even though encode()
    // itself never emits it.
    TEST_CASE("LLZeroCode: decode accepts the doubled-marker wrap form", "[LLZeroCode]")
    {
        // header (1 byte, FLAG set) + [0x00 marker][0x00 extra-wrap-marker][terminator=5]
        // decoded body length = 1 (marker) + 1 (extra marker) + 255 (memset) + (5 - 1) = 261
        std::vector<U8> encoded = {(U8)LLZeroCode::FLAG, 0x00, 0x00, 0x05};
        std::vector<U8> dec(1024, 0xDD);
        bool overflow = false;
        U32 dec_size = LLZeroCode::decode(encoded.data(), (U32)encoded.size(),
                                           dec.data(), (U32)dec.size(), 1, overflow);
        CHECK_FALSE(overflow);
        CHECK(dec_size == (U32)262);
        UNSCOPED_INFO("decode clears FLAG on the header");
        CHECK(dec[0] == (U8)0x00);
        for (U32 i = 1; i < dec_size; ++i)
        {
            CAPTURE(i);
            REQUIRE(dec[i] == (U8)0);
        }
    }

    // decode() is a no-op (returns 0, does not touch dst) when FLAG is not set.
    TEST_CASE("LLZeroCode: decode does nothing when FLAG is not set", "[LLZeroCode]")
    {
        std::vector<U8> src = makeBuffer(6, 0x00, {1,2,3,0,0,0});
        std::vector<U8> dec(64, 0xCC);
        bool overflow = false;
        U32 dec_size = LLZeroCode::decode(src.data(), (U32)src.size(),
                                           dec.data(), (U32)dec.size(), 6, overflow);
        CHECK(dec_size == (U32)0);
        CHECK_FALSE(overflow);
    }

    // encode() refuses to write into an undersized destination buffer.
    TEST_CASE("LLZeroCode: encode refuses an undersized destination", "[LLZeroCode]")
    {
        std::vector<U8> body(50, 0);
        std::vector<U8> src = makeBuffer(1, 0, body);
        std::vector<U8> enc(src.size(), 0); // smaller than the required 2 * src_size
        S32 r = LLZeroCode::encode(src.data(), (U32)src.size(), enc.data(), (U32)enc.size(), 1);
        CHECK(r < 0);
    }

    // decode() reports overflow (rather than writing out of bounds) when dst is too small,
    // whether the destination is smaller than a single byte's worth of headroom...
    TEST_CASE("LLZeroCode: decode flags overflow into a destination only the header fits", "[LLZeroCode]")
    {
        std::vector<U8> body(500, 0);
        std::vector<U8> src = makeBuffer(1, 0, body);
        std::vector<U8> enc(2 * src.size() + 16, 0);
        S32 enc_size = LLZeroCode::encode(src.data(), (U32)src.size(), enc.data(), (U32)enc.size(), 1);
        CHECK(enc_size >= 0);

        std::vector<U8> dec(1, 0); // capacity == header_size exactly
        bool overflow = false;
        U32 dec_size = LLZeroCode::decode(enc.data(), (U32)enc_size,
                                           dec.data(), (U32)dec.size(), 1, overflow);
        CHECK(overflow);
        UNSCOPED_INFO("the partial output is discarded");
        CHECK(dec_size == 0u);
    }

    // ...or comfortably larger than 256 bytes but still short of the true decoded size.
    TEST_CASE("LLZeroCode: decode flags overflow into a destination short of the output", "[LLZeroCode]")
    {
        std::vector<U8> body(500, 0);
        std::vector<U8> src = makeBuffer(1, 0, body);
        std::vector<U8> enc(2 * src.size() + 16, 0);
        S32 enc_size = LLZeroCode::encode(src.data(), (U32)src.size(), enc.data(), (U32)enc.size(), 1);
        CHECK(enc_size >= 0);

        std::vector<U8> dec(300, 0); // > 256, but less than the true decoded size (~501)
        bool overflow = false;
        U32 dec_size = LLZeroCode::decode(enc.data(), (U32)enc_size,
                                           dec.data(), (U32)dec.size(), 1, overflow);
        CHECK(overflow);
        UNSCOPED_INFO("the partial output is discarded");
        CHECK(dec_size == 0u);
    }

    // A pathological alternating zero/non-zero pattern grows under zero-coding
    // (every isolated zero costs 2 output bytes for 1 input byte), so encode
    // must refuse it and leave the original buffer in use.
    TEST_CASE("LLZeroCode: encode refuses alternating zero and non-zero bytes", "[LLZeroCode]")
    {
        std::vector<U8> body;
        for (int i = 0; i < 200; ++i)
        {
            body.push_back(0);
            body.push_back((U8)(i + 1));
        }
        require_round_trip("alternating pattern", 1, 0x00, body, false);
    }
}
