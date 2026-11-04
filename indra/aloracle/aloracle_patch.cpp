/**
 * @file aloracle_patch.cpp
 * @brief aloracle's function over llmessage's terrain patch decoding.
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

#include "aloracleboundary.h"

#include "llbitpack.h"
#include "llsd.h"
#include "patch_code.h"
#include "patch_dct.h"

#include <bit>
#include <limits>
#include <string>
#include <vector>

namespace
{
    // The most bytes one step of the decode can read: a patch of 32 by 32
    // whose every coefficient takes its 3 bits and the most word bits, 17,
    // and a byte it had begun. A header reads fewer.
    constexpr size_t STEP_BYTES = (LARGE_PATCH_SIZE * LARGE_PATCH_SIZE * (3 + 17)) / 8 + 1;

    LLSD describe(const LLPatchHeader& header, const std::vector<F32>& heights)
    {
        LLSD::Binary bytes;
        bytes.reserve(heights.size() * 4);
        for (const F32 height : heights)
        {
            const U32 bits = std::bit_cast<U32>(height);
            bytes.push_back(U8(bits));
            bytes.push_back(U8(bits >> 8));
            bytes.push_back(U8(bits >> 16));
            bytes.push_back(U8(bits >> 24));
        }

        LLSD value;
        value["quant_wbits"] = (S32)header.quant_wbits;
        value["dc_offset"] = header.dc_offset;
        value["range"] = (S32)header.range;
        value["patchids"] = (S32)header.patchids;
        // As LLSurface::decompressDCTPatch places it.
        value["x"] = (S32)(header.patchids >> 5);
        value["y"] = (S32)(header.patchids & 0x1F);
        value["heights"] = bytes;
        return value;
    }
}

int32_t aloracle_layer_decode(int32_t type, const uint8_t* data, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(value) || size > (size_t)std::numeric_limits<S32>::max()
            || !aloracle::take(data, size, bytes)
            || (type != ALORACLE_LAYER_LAND && type != ALORACLE_LAYER_WIND && type != ALORACLE_LAYER_CLOUD))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        // LLBitPack reads on past its size. Each step reads here into zeros
        // the library owns, and one that read a byte past the data refuses
        // the data before anything it read is used.
        bytes.resize(size + STEP_BYTES, '\0');
        LLBitPack bitpack(reinterpret_cast<U8*>(bytes.data()), (U32)size);
        const auto within = [&] { return bitpack.mBufferSize <= size; };

        LLGroupHeader group;
        decode_patch_group_header(bitpack, &group);
        if (!within())
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLSD layer;
        layer["group"]["stride"] = (S32)group.stride;
        layer["group"]["patch_size"] = (S32)group.patch_size;
        layer["group"]["layer_type"] = (S32)group.layer_type;
        layer["patches"] = LLSD::emptyArray();

        // LLVLManager::unpackData reads no further into clouds.
        if (type != ALORACLE_LAYER_CLOUD)
        {
            if (group.patch_size != NORMAL_PATCH_SIZE && group.patch_size != LARGE_PATCH_SIZE)
            {
                return ALORACLE_BAD_ARGUMENT;
            }
            init_patch_decompressor(group.patch_size);
            // Each patch decompresses into heights of its own size.
            group.stride = group.patch_size;
            set_group_of_patch_header(&group);

            // Land to the end of its patches, wind its velocity along x then
            // along y.
            for (S32 read = 0; type == ALORACLE_LAYER_LAND || read < 2; ++read)
            {
                LLPatchHeader header;
                decode_patch_header(bitpack, &header);
                if (!within())
                {
                    return ALORACLE_BAD_ARGUMENT;
                }
                if (header.quant_wbits == END_OF_PATCHES)
                {
                    if (type == ALORACLE_LAYER_LAND)
                    {
                        break;
                    }
                    // decode_patch_header leaves the word bits as the patch
                    // before set them, and LLWind refuses the layer.
                    return ALORACLE_BAD_ARGUMENT;
                }

                S32 coefficients[LARGE_PATCH_SIZE * LARGE_PATCH_SIZE];
                decode_patch(bitpack, coefficients);
                if (!within())
                {
                    return ALORACLE_BAD_ARGUMENT;
                }
                std::vector<F32> heights((size_t)group.patch_size * group.patch_size);
                decompress_patch(heights.data(), coefficients, &header);
                layer["patches"].append(describe(header, heights));
            }
        }
        return aloracle::giveValue(layer, value);
    });
}
