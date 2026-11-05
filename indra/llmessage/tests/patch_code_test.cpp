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

    // A layer's bytes as a simulator packs them: the group header, a patch for
    // each ID and the end code, which wind goes without.
    std::vector<U8> layerOfPatches(S32 layer_type, std::vector<F32> heights, const std::vector<U16>& patch_ids,
                                   bool end_code = true)
    {
        init_patch_compressor(NORMAL_PATCH_SIZE, NORMAL_PATCH_SIZE, layer_type);

        std::vector<U8> bytes(PATCH_CELLS * 16);
        LLBitPack writer(bytes.data(), (U32)bytes.size());
        init_patch_coding(writer);

        LLGroupHeader group;
        get_patch_group_header(&group);
        code_patch_group_header(writer, &group);

        for (const U16 patch_id : patch_ids)
        {
            LLPatchHeader header;
            F32 highest = 0.f;
            F32 lowest = 0.f;
            prescan_patch(heights.data(), &header, highest, lowest);
            S32 coefficients[LARGE_PATCH_SIZE * LARGE_PATCH_SIZE];
            compress_patch(heights.data(), coefficients, &header, 10);
            header.patchids = patch_id;
            code_patch_header(writer, &header, coefficients);
            code_patch(writer, coefficients, 0);
        }

        if (end_code)
        {
            code_end_of_data(writer);
        }
        end_patch_coding(writer);
        bytes.resize(writer.mBufferSize);
        return bytes;
    }

    // What LLSurface::decompressDCTPatch reads of a land layer: the coefficients
    // of each whole patch, and the bytes read as it ended. A read that never
    // comes to the end of its patches stops at the most any layer here holds.
    struct LandRead
    {
        bool came_to_end = false;
        std::vector<std::vector<S32>> patches;
        std::vector<U32> patch_ends;
    };

    LandRead readLand(LLBitPack& reader)
    {
        constexpr S32 MOST_PATCHES = 8;

        LandRead read;
        LLGroupHeader group;
        if (!decode_layer_group_header(reader, &group))
        {
            read.came_to_end = true;
            return read;
        }
        while ((S32)read.patches.size() < MOST_PATCHES)
        {
            LLPatchHeader header;
            std::vector<S32> coefficients(PATCH_CELLS);
            if (!decode_expected_patch(reader, &header, coefficients.data()))
            {
                read.came_to_end = true;
                break;
            }
            read.patches.push_back(coefficients);
            read.patch_ends.push_back(reader.mBufferSize);
        }
        return read;
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
        std::vector<U8> bytes = layerOfPatches('L', sent, { (LAST_PATCH << 5) | LAST_PATCH });

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

    TEST_CASE("patch_code: a group header the data ends inside may not be decoded", "[patch_code]")
    {
        // A patch size of 16 in the data, with the type after it past the end
        // at a length of three.
        U8 bytes[] = { 0x08, 0x01, NORMAL_PATCH_SIZE, 'L' };
        for (U32 length = 0; length < sizeof(bytes); ++length)
        {
            CAPTURE(length);
            LLBitPack reader(bytes, length);
            LLGroupHeader group;
            CHECK_FALSE(decode_layer_group_header(reader, &group));
        }

        LLBitPack reader(bytes, sizeof(bytes));
        LLGroupHeader group;
        CHECK(decode_layer_group_header(reader, &group));
    }

    TEST_CASE("patch_code: a patch header the data ends inside reads as the end of the patches", "[patch_code]")
    {
        // The encoder takes the patch size from the group header it last packed.
        U8 group_bytes[4];
        LLBitPack group_writer(group_bytes, sizeof(group_bytes));
        LLGroupHeader group{};
        group.patch_size = NORMAL_PATCH_SIZE;
        code_patch_group_header(group_writer, &group);

        // A header of 66 bits, which take nine bytes.
        U8 bytes[16] = { 0 };
        LLBitPack writer(bytes, sizeof(bytes));
        LLPatchHeader sent;
        sent.quant_wbits = 0x88;
        sent.dc_offset = 20.f;
        sent.range = 10;
        sent.patchids = (3 << 5) | 7;
        S32 coefficients[PATCH_CELLS] = { 0 };
        code_patch_header(writer, &sent, coefficients);
        const U32 length = writer.flushBitPack();
        REQUIRE(length == 9);

        for (U32 cut = 0; cut < length; ++cut)
        {
            CAPTURE(cut);
            LLBitPack reader(bytes, cut);
            LLPatchHeader header;
            decode_patch_header(reader, &header);
            CHECK(header.quant_wbits == END_OF_PATCHES);
            CHECK(header.dc_offset == 0.f);
            CHECK(header.range == 0);
            CHECK(header.patchids == 0);
        }

        LLBitPack reader(bytes, length);
        LLPatchHeader header;
        decode_patch_header(reader, &header);
        CHECK(header.quant_wbits == sent.quant_wbits);
        CHECK(header.dc_offset == sent.dc_offset);
        CHECK(header.range == sent.range);
        CHECK(header.patchids == sent.patchids);
        CHECK_FALSE(reader.readPastEnd());
    }

    TEST_CASE("patch_code: a land layer cut short at any byte comes to an end with the patches whole before the cut", "[patch_code]")
    {
        std::vector<U8> bytes = layerOfPatches('L', slopedPatch(), { 0, 1, 2 });

        LLBitPack whole_reader(bytes.data(), (U32)bytes.size());
        const LandRead whole = readLand(whole_reader);
        REQUIRE(whole.came_to_end);
        REQUIRE(whole.patches.size() == 3);
        CHECK_FALSE(whole_reader.readPastEnd());

        for (U32 cut = 0; cut < bytes.size(); ++cut)
        {
            CAPTURE(cut);

            size_t whole_before = 0;
            while (whole_before < whole.patch_ends.size() && whole.patch_ends[whole_before] <= cut)
            {
                ++whole_before;
            }

            LLBitPack reader(bytes.data(), cut);
            const LandRead read = readLand(reader);
            CHECK(read.came_to_end);
            CHECK(reader.readPastEnd());
            REQUIRE(read.patches.size() == whole_before);
            for (size_t patch = 0; patch < whole_before; ++patch)
            {
                CHECK(read.patches[patch] == whole.patches[patch]);
            }
        }
    }

    TEST_CASE("patch_code: a wind layer holds both its patches only where it is whole", "[patch_code]")
    {
        std::vector<U8> bytes = layerOfPatches('7', slopedPatch(), { 0, 0 }, false);

        // As LLWind::decompress reads it, which applies the patches only where
        // this holds.
        const auto bothPatches = [&](U32 length)
        {
            LLBitPack reader(bytes.data(), length);
            LLGroupHeader group;
            decode_patch_group_header(reader, &group);
            LLPatchHeader header_x;
            LLPatchHeader header_y;
            S32 buffer_x[PATCH_CELLS];
            S32 buffer_y[PATCH_CELLS];
            return decode_expected_patch(reader, &header_x, buffer_x)
                && decode_expected_patch(reader, &header_y, buffer_y);
        };

        CHECK(bothPatches((U32)bytes.size()));
        for (U32 cut = 0; cut < bytes.size(); ++cut)
        {
            CAPTURE(cut);
            CHECK_FALSE(bothPatches(cut));
        }
    }
}
