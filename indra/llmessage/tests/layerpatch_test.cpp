/**
 * @file layerpatch_test.cpp
 * @brief Tests of a layer's patches, read and written, at its end code.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llbitpack.h"

#include "../patch_code.h"
#include "../patch_dct.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
    constexpr S32 PATCH_CELLS = NORMAL_PATCH_SIZE * NORMAL_PATCH_SIZE;
    constexpr S32 UNTOUCHED = -12345;

    // A patch's coefficients: a few near its start, then zeros.
    std::vector<S32> coefficientsOfAPatch()
    {
        std::vector<S32> coefficients(PATCH_CELLS, 0);
        coefficients[0] = 100;
        coefficients[1] = -60;
        coefficients[3] = 7;
        return coefficients;
    }

    struct Layer
    {
        std::vector<U8> mBytes = std::vector<U8>(PATCH_CELLS * 8, 0);
        LLBitPack mWriter{ mBytes.data(), (U32)mBytes.size() };

        Layer()
        {
            init_patch_coding(mWriter);
            LLGroupHeader group;
            group.stride = NORMAL_PATCH_SIZE;
            group.patch_size = NORMAL_PATCH_SIZE;
            group.layer_type = '7';
            code_patch_group_header(mWriter, &group);
        }

        void addPatch(std::vector<S32> coefficients)
        {
            LLPatchHeader header;
            header.dc_offset = 1.f;
            header.range = 2;
            header.quant_wbits = 0x66;
            header.patchids = 0;
            code_patch_header(mWriter, &header, coefficients.data());
            code_patch(mWriter, coefficients.data(), 0);
        }

        // Ends the layer and reads its group header back, leaving the reader at its first patch.
        LLBitPack read(LLGroupHeader& group)
        {
            code_end_of_data(mWriter);
            end_patch_coding(mWriter);
            LLBitPack reader(mBytes.data(), mWriter.mBufferSize);
            init_patch_decoding(reader);
            decode_patch_group_header(reader, &group);
            return reader;
        }
    };

    TEST_CASE("layer patch: an expected patch is read with its own word bits", "[patch_code]")
    {
        Layer layer;
        const std::vector<S32> sent = coefficientsOfAPatch();
        layer.addPatch(sent);
        layer.addPatch(sent);

        LLGroupHeader group;
        LLBitPack reader = layer.read(group);
        for (S32 patch = 0; patch < 2; ++patch)
        {
            CAPTURE(patch);
            LLPatchHeader header;
            std::vector<S32> received(PATCH_CELLS, UNTOUCHED);
            REQUIRE(decode_expected_patch(reader, &header, received.data()));
            CHECK(received == sent);
        }
    }

    TEST_CASE("layer patch: the end code where a patch is expected reads no coefficients", "[patch_code]")
    {
        SECTION("as the first patch")
        {
            Layer layer;
            LLGroupHeader group;
            LLBitPack reader = layer.read(group);

            LLPatchHeader header;
            std::vector<S32> received(PATCH_CELLS, UNTOUCHED);
            CHECK_FALSE(decode_expected_patch(reader, &header, received.data()));
            CHECK(header.quant_wbits == END_OF_PATCHES);
            CHECK(std::all_of(received.begin(), received.end(), [](S32 value) { return value == UNTOUCHED; }));
        }

        SECTION("after a patch whose word bits it would be read with")
        {
            Layer layer;
            layer.addPatch(coefficientsOfAPatch());
            LLGroupHeader group;
            LLBitPack reader = layer.read(group);

            LLPatchHeader header;
            std::vector<S32> received(PATCH_CELLS, UNTOUCHED);
            REQUIRE(decode_expected_patch(reader, &header, received.data()));

            std::fill(received.begin(), received.end(), UNTOUCHED);
            CHECK_FALSE(decode_expected_patch(reader, &header, received.data()));
            CHECK(header.quant_wbits == END_OF_PATCHES);
            CHECK(std::all_of(received.begin(), received.end(), [](S32 value) { return value == UNTOUCHED; }));
        }
    }

    // A writer at a patch, past its group header, with nothing of a patch written.
    struct Writer
    {
        std::vector<U8> mBytes = std::vector<U8>(PATCH_CELLS * 8, 0);
        LLBitPack mPack{ mBytes.data(), (U32)mBytes.size() };

        Writer()
        {
            init_patch_coding(mPack);
            LLGroupHeader group;
            group.stride = NORMAL_PATCH_SIZE;
            group.patch_size = NORMAL_PATCH_SIZE;
            group.layer_type = 'L';
            code_patch_group_header(mPack, &group);
        }

        // Writes a header that leaves word bits of 3, under a quantization of 7.
        void atWordBitsOfThree()
        {
            std::vector<S32> small(PATCH_CELLS, 0);
            LLPatchHeader header;
            header.dc_offset = 0.f;
            header.range = 1;
            header.quant_wbits = 0x52;
            header.patchids = 0;
            REQUIRE(code_patch_header(mPack, &header, small.data()));
            REQUIRE((header.quant_wbits & 0xf) + 2 == 3);
        }
    };

    TEST_CASE("layer patch: no quantization byte the encoder writes is the end code", "[patch_code]")
    {
        const U32 group_bits = Writer().mPack.mTotalBits;
        for (S32 byte = 0; byte < 256; ++byte)
        {
            for (S32 magnitude : { 0, 4, 100 })
            {
                CAPTURE(byte, magnitude);
                Writer writer;
                std::vector<S32> coefficients(PATCH_CELLS, 0);
                coefficients[0] = magnitude;
                LLPatchHeader header;
                header.dc_offset = 0.f;
                header.range = 1;
                header.quant_wbits = (U8)byte;
                header.patchids = 0;

                if (code_patch_header(writer.mPack, &header, coefficients.data()))
                {
                    CHECK(header.quant_wbits != END_OF_PATCHES);
                    CHECK(writer.mPack.mTotalBits == group_bits + 8 + 32 + 16 + 10);
                }
                else
                {
                    UNSCOPED_INFO("the header that would read as the end code alone is refused");
                    CHECK(header.quant_wbits == END_OF_PATCHES);
                    CHECK(writer.mPack.mTotalBits == group_bits);
                }
            }
        }
    }

    TEST_CASE("layer patch: a quantization of 8 with word bits of 3 is refused and word bits of 4 are not", "[patch_code]")
    {
        Writer writer;
        const U32 before = writer.mPack.mTotalBits;

        // Word bits of 4 start at 2 and an empty patch takes them to 3.
        std::vector<S32> empty(PATCH_CELLS, 0);
        LLPatchHeader header;
        header.dc_offset = 0.f;
        header.range = 1;
        header.quant_wbits = 0x62;
        header.patchids = 0;
        CHECK_FALSE(code_patch_header(writer.mPack, &header, empty.data()));
        CHECK(writer.mPack.mTotalBits == before);

        // A magnitude in bit 3 takes them to 4.
        std::vector<S32> wider(PATCH_CELLS, 0);
        wider[0] = 8;
        header.quant_wbits = 0x62;
        CHECK(code_patch_header(writer.mPack, &header, wider.data()));
        CHECK(header.quant_wbits == 0x62);
    }

    TEST_CASE("layer patch: a magnitude past its word bits is refused with nothing written", "[patch_code]")
    {
        const S32 magnitude = GENERATE(8, -8, 9, -9, 1000, -1000, std::numeric_limits<S32>::min(), std::numeric_limits<S32>::max());
        CAPTURE(magnitude);

        Writer writer;
        writer.atWordBitsOfThree();
        const U32 before = writer.mPack.mTotalBits;

        // The first coefficients fit: refusing the last leaves none of them written.
        std::vector<S32> coefficients(PATCH_CELLS, 0);
        coefficients[0] = 7;
        coefficients[1] = -7;
        coefficients[PATCH_CELLS - 1] = magnitude;
        CHECK_FALSE(code_patch(writer.mPack, coefficients.data(), 0));
        CHECK(writer.mPack.mTotalBits == before);
    }

    TEST_CASE("layer patch: the largest magnitude its word bits hold reads back", "[patch_code]")
    {
        Writer writer;
        writer.atWordBitsOfThree();

        std::vector<S32> sent(PATCH_CELLS, 0);
        sent[0] = 7;
        sent[1] = -7;
        sent[2] = 1;
        REQUIRE(code_patch(writer.mPack, sent.data(), 0));
        code_end_of_data(writer.mPack);
        end_patch_coding(writer.mPack);

        LLBitPack reader(writer.mBytes.data(), writer.mPack.mBufferSize);
        init_patch_decoding(reader);
        LLGroupHeader group;
        decode_patch_group_header(reader, &group);
        LLPatchHeader header;
        std::vector<S32> received(PATCH_CELLS, UNTOUCHED);
        REQUIRE(decode_expected_patch(reader, &header, received.data()));
        CHECK(received == sent);
    }

    TEST_CASE("layer patch: a magnitude post quantization leaves unwritten is not refused", "[patch_code]")
    {
        Writer writer;
        writer.atWordBitsOfThree();

        // A postquant of PATCH_CELLS - 2 writes coefficients 0 and 1 and clears the one after.
        std::vector<S32> coefficients(PATCH_CELLS, 0);
        coefficients[0] = 7;
        coefficients[2] = 1000;
        coefficients[5] = 1000;
        CHECK(code_patch(writer.mPack, coefficients.data(), PATCH_CELLS - 2));
        CHECK(coefficients[2] == 0);
    }
}
