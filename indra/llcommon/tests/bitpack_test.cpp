/**
 * @file bitpack_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief llstreamtools test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "../llbitpack.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"


namespace
{
    TEST_CASE("LLBitPack: a string packed then unpacked comes back unchanged", "[LLBitPack]")
    {
        U8 packbuffer[255];
        U8 unpackbuffer[255];
        int pack_bufsize = 0;
        int unpack_bufsize = 0;

        LLBitPack bitpack(packbuffer, 255);

        char str[] = "SecondLife is a 3D virtual world";
        int len = sizeof(str);
        pack_bufsize = bitpack.bitPack((U8*) str, len*8);
        pack_bufsize = bitpack.flushBitPack();

        LLBitPack bitunpack(packbuffer, pack_bufsize*8);
        unpack_bufsize = bitunpack.bitUnpack(unpackbuffer, len*8);
        CHECK(len == unpack_bufsize);
        CHECK_THAT(ll_test::Memory(str, len), ll_test::MemoryMatches(unpackbuffer, unpack_bufsize));
    }

    TEST_CASE("LLBitPack: a packed string unpacks a byte at a time", "[LLBitPack]")
    {
        U8 packbuffer[255];
        U8 unpackbuffer[255];
        int pack_bufsize = 0;

        LLBitPack bitpack(packbuffer, 255);

        char str[] = "SecondLife";
        int len = sizeof(str);
        pack_bufsize = bitpack.bitPack((U8*) str, len*8);
        pack_bufsize = bitpack.flushBitPack();

        LLBitPack bitunpack(packbuffer, pack_bufsize*8);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[0]);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[1]);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[2]);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[3]);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[4]);
        bitunpack.bitUnpack(&unpackbuffer[0], 8);
        CHECK(unpackbuffer[0] == (U8) str[5]);
        bitunpack.bitUnpack(unpackbuffer, 8*4); // Life
        CHECK_THAT(ll_test::Memory(unpackbuffer, 4), ll_test::MemoryMatches(str+6, 4));
    }

    TEST_CASE("LLBitPack: a U32 packed then unpacked comes back unchanged", "[LLBitPack]")
    {
        U8 packbuffer[255];
        int pack_bufsize = 0;

        LLBitPack bitpack(packbuffer, 255);
        U32 num = 0x41fab67a;
        pack_bufsize = bitpack.bitPack((U8*)&num, 8*sizeof(U32));
        pack_bufsize = bitpack.flushBitPack();

        LLBitPack bitunpack(packbuffer, pack_bufsize*8);
        U32 res = 0;
        // since packing and unpacking is done on same machine in the unit test run,
        // endianness should not matter
        bitunpack.bitUnpack((U8*) &res, sizeof(res)*8);
        CHECK(num == res);
    }

    TEST_CASE("LLBitPack: a read past the data takes zeros and not what lies after it", "[LLBitPack]")
    {
        U8 buffer[] = { 0xA5, 0x5A, 0xFF, 0xFF };
        LLBitPack reader(buffer, 2);

        U8 read = 0;
        reader.bitUnpack(&read, 8);
        CHECK(read == 0xA5);
        reader.bitUnpack(&read, 8);
        CHECK(read == 0x5A);
        CHECK_FALSE(reader.readPastEnd());

        read = 0x11;
        reader.bitUnpack(&read, 8);
        CHECK(read == 0);
        CHECK(reader.readPastEnd());
    }

    TEST_CASE("LLBitPack: bits of the data's last byte are within it until they run out", "[LLBitPack]")
    {
        U8 buffer[] = { 0xFF, 0xFF };
        LLBitPack reader(buffer, 1);

        U8 read = 0;
        reader.bitUnpack(&read, 3);
        CHECK(read == 0x07);
        reader.bitUnpack(&read, 5);
        CHECK(read == 0x1F);
        CHECK_FALSE(reader.readPastEnd());

        reader.bitUnpack(&read, 1);
        CHECK(read == 0);
        CHECK(reader.readPastEnd());
    }

    TEST_CASE("LLBitPack: a read that crosses the data's end keeps the bits it had", "[LLBitPack]")
    {
        U8 buffer[] = { 0xFF, 0xFF };
        LLBitPack reader(buffer, 1);

        U8 read[2] = { 0xEE, 0xEE };
        reader.bitUnpack(read, 2 * 8);
        CHECK(read[0] == 0xFF);
        CHECK(read[1] == 0);
        CHECK(reader.readPastEnd());
    }

    TEST_CASE("LLBitPack: every byte a read takes past the data is zero and counted", "[LLBitPack]")
    {
        U8 buffer[] = { 0xFF, 0xFF, 0xFF, 0xFF };
        LLBitPack reader(buffer, 1);

        U8 read[4] = { 0xEE, 0xEE, 0xEE, 0xEE };
        CHECK(reader.bitUnpack(read, 4 * 8) == 4);
        CHECK(read[0] == 0xFF);
        CHECK(read[1] == 0);
        CHECK(read[2] == 0);
        CHECK(read[3] == 0);
        CHECK(reader.mBufferSize == 4);
    }

    TEST_CASE("LLBitPack: data of no bytes reads as zeros from no buffer", "[LLBitPack]")
    {
        LLBitPack reader(nullptr, 0);
        CHECK_FALSE(reader.readPastEnd());

        U8 read = 0x11;
        reader.bitUnpack(&read, 8);
        CHECK(read == 0);
        CHECK(reader.readPastEnd());
    }

    TEST_CASE("LLBitPack: resetting reads the data again from its start", "[LLBitPack]")
    {
        U8 buffer[] = { 0xA5 };
        LLBitPack reader(buffer, 1);

        U8 read[2] = { 0 };
        reader.bitUnpack(read, 2 * 8);
        REQUIRE(reader.readPastEnd());

        reader.resetBitPacking();
        CHECK_FALSE(reader.readPastEnd());
        reader.bitUnpack(read, 8);
        CHECK(read[0] == 0xA5);
        CHECK_FALSE(reader.readPastEnd());
    }
}
