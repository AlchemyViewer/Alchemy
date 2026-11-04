/**
 * @file patch_code_test.cpp
 * @brief Tests of the group header and patches of a layer off the wire.
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
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <vector>

namespace
{
    constexpr S32 PATCH_CELLS = NORMAL_PATCH_SIZE * NORMAL_PATCH_SIZE;
    constexpr F32 UNTOUCHED = -12345.f;

    // A patch that slopes both ways, its height at each column and row.
    std::vector<F32> slopedPatch()
    {
        std::vector<F32> heights(PATCH_CELLS);
        for (S32 row = 0; row < NORMAL_PATCH_SIZE; ++row)
        {
            for (S32 column = 0; column < NORMAL_PATCH_SIZE; ++column)
            {
                heights[row * NORMAL_PATCH_SIZE + column] = 20.f + 0.5f * column + 0.25f * row;
            }
        }
        return heights;
    }

    // A land layer's bytes as a simulator packs them: the group header, one
    // patch and the end code.
    std::vector<U8> layerOfOnePatch(std::vector<F32> heights, U16 patch_ids)
    {
        init_patch_compressor(NORMAL_PATCH_SIZE, NORMAL_PATCH_SIZE, 'L');

        std::vector<U8> bytes(PATCH_CELLS * 4);
        LLBitPack writer(bytes.data(), (U32)bytes.size());
        init_patch_coding(writer);

        LLGroupHeader group;
        get_patch_group_header(&group);
        code_patch_group_header(writer, &group);

        LLPatchHeader header;
        F32 highest = 0.f;
        F32 lowest = 0.f;
        prescan_patch(heights.data(), &header, highest, lowest);
        S32 coefficients[LARGE_PATCH_SIZE * LARGE_PATCH_SIZE];
        compress_patch(heights.data(), coefficients, &header, 10);
        header.patchids = patch_ids;
        code_patch_header(writer, &header, coefficients);
        code_patch(writer, coefficients, 0);

        code_end_of_data(writer);
        end_patch_coding(writer);
        bytes.resize(writer.mBufferSize);
        return bytes;
    }

    TEST_CASE("patch_code: a layer's group header says a patch size of sixteen alone may be decoded", "[patch_code]")
    {
        const S32 size = GENERATE(range(0, 256));
        CAPTURE(size);

        // A stride of 264, the patch size and the layer type.
        U8 bytes[] = { 0x08, 0x01, (U8)size, 'L' };
        LLBitPack reader(bytes, sizeof(bytes));
        LLGroupHeader group;
        const bool decodable = decode_layer_group_header(reader, &group);

        CHECK(decodable == (size == 16));
        CHECK(group.stride == 264);
        CHECK(group.patch_size == size);
        CHECK(group.layer_type == 'L');
        CHECK(reader.mBufferSize == sizeof(bytes));
    }

    TEST_CASE("patch_code: a patch of sixteen stays in its place at land's stride on the region's last row", "[patch_code]")
    {
        // As LLSurface lays out a region: 257 by 257, one row and column more
        // than its 16 by 16 patches hold. The patch at column and row 15 ends
        // one row and column short of the end.
        constexpr S32 GRIDS_PER_EDGE = 257;
        constexpr S32 LAST_PATCH = 15;
        const std::vector<F32> sent = slopedPatch();
        std::vector<U8> bytes = layerOfOnePatch(sent, (LAST_PATCH << 5) | LAST_PATCH);

        LLBitPack reader(bytes.data(), (U32)bytes.size());
        init_patch_decoding(reader);
        LLGroupHeader group;
        REQUIRE(decode_layer_group_header(reader, &group));
        init_patch_decompressor(group.patch_size);
        group.stride = GRIDS_PER_EDGE;
        set_group_of_patch_header(&group);

        LLPatchHeader header;
        decode_patch_header(reader, &header);
        CHECK((header.patchids >> 5) == LAST_PATCH);
        CHECK((header.patchids & 0x1F) == LAST_PATCH);

        S32 coefficients[LARGE_PATCH_SIZE * LARGE_PATCH_SIZE];
        decode_patch(reader, coefficients);

        std::vector<F32> surface(GRIDS_PER_EDGE * GRIDS_PER_EDGE, UNTOUCHED);
        const S32 first = (LAST_PATCH * NORMAL_PATCH_SIZE) * GRIDS_PER_EDGE + LAST_PATCH * NORMAL_PATCH_SIZE;
        decompress_patch(&surface[first], coefficients, &header);

        S32 outside_written = 0;
        S32 off_by_much = 0;
        for (S32 row = 0; row < GRIDS_PER_EDGE; ++row)
        {
            for (S32 column = 0; column < GRIDS_PER_EDGE; ++column)
            {
                const F32 height = surface[row * GRIDS_PER_EDGE + column];
                const S32 patch_row = row - LAST_PATCH * NORMAL_PATCH_SIZE;
                const S32 patch_column = column - LAST_PATCH * NORMAL_PATCH_SIZE;
                if (patch_row < 0 || patch_row >= NORMAL_PATCH_SIZE || patch_column < 0 || patch_column >= NORMAL_PATCH_SIZE)
                {
                    outside_written += (height != UNTOUCHED);
                }
                else
                {
                    off_by_much += !(std::fabs(height - sent[patch_row * NORMAL_PATCH_SIZE + patch_column]) < 0.5f);
                }
            }
        }
        CHECK(outside_written == 0);
        CHECK(off_by_much == 0);

        decode_patch_header(reader, &header);
        CHECK(header.quant_wbits == END_OF_PATCHES);
    }
}
