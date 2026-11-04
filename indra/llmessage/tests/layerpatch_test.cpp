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

#include <algorithm>
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
}
