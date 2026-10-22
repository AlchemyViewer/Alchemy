/**
 * @file alj2clayout.cpp
 * @brief ALJ2CLayout: a JPEG 2000 codestream's layout, read from its headers
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

#include "alj2clayout.h"

#include "llimage.h"

namespace
{
    constexpr U8 MARKER = 0xFF;
    constexpr U8 SOC = 0x4F;
    constexpr U8 SIZ = 0x51;
    constexpr U8 COD = 0x52;
    constexpr U8 SOT = 0x90;

    // The marker, Lsot, Isot, Psot, TPsot and TNsot.
    constexpr S32 SOT_FIELDS_SIZE = 12;
    // SOT's fields and the SOD that ends the shortest tile-part header.
    constexpr S32 MIN_TILE_PART_SIZE = SOT_FIELDS_SIZE + 2;
    constexpr S32 EOC_SIZE = 2;

    constexpr S32 SIZ_MIN_BODY = 38;
    constexpr S32 COD_MIN_BODY = 10;

    // The fraction of a codestream that decodes at each discard level within 45 dB PSNR of
    // the whole codestream's decode at that level, by the texture's longer side, for nine in
    // ten Second Life textures of each size whether single or multi-layer. The quality
    // layers' byte tiers are fixed, so they take a larger share of a smaller file.
    struct StreamingFractions
    {
        S32 mMaxSide;
        F64 mFraction[MAX_DISCARD_LEVEL + 1];
    };
    constexpr StreamingFractions STREAMING_FRACTIONS[] = {
        {     128, { 1.0, 0.90, 0.75, 0.60, 0.45, 0.45 } },
        {     256, { 1.0, 0.75, 0.50, 0.30, 0.14, 0.10 } },
        {     512, { 1.0, 0.70, 0.45, 0.20, 0.10, 0.06 } },
        {    1024, { 1.0, 0.65, 0.40, 0.12, 0.08, 0.04 } },
        { S32_MAX, { 1.0, 0.65, 0.30, 0.12, 0.05, 0.03 } },
    };

    F64 streamingFraction(S32 side, S32 discard)
    {
        for (const StreamingFractions& row : STREAMING_FRACTIONS)
        {
            if (side <= row.mMaxSide)
            {
                return row.mFraction[discard];
            }
        }
        return 1.0;
    }

    U32 read16(const U8* p)
    {
        return (U32(p[0]) << 8) | U32(p[1]);
    }

    U32 read32(const U8* p)
    {
        return (U32(p[0]) << 24) | (U32(p[1]) << 16) | (U32(p[2]) << 8) | U32(p[3]);
    }

    U64 ceilDiv(U64 numerator, U64 denominator)
    {
        return (numerator + denominator - 1) / denominator;
    }
}

// static
ALJ2CLayout ALJ2CLayout::parse(const U8* data, S32 size)
{
    if (!data || size < 4 || data[0] != MARKER || data[1] != SOC)
    {
        return {};
    }

    ALJ2CLayout layout;
    bool have_siz = false;
    bool have_cod = false;
    S32 pos = 2;
    while (pos + 4 <= size && data[pos + 1] != SOT)
    {
        if (data[pos] != MARKER)
        {
            return {};
        }

        // The segment length counts itself but not the marker.
        const S32 length = S32(read16(data + pos + 2));
        if (length < 2 || pos + 2 + length > size)
        {
            return {};
        }

        const U8* body = data + pos + 4;
        const S32 body_size = length - 2;
        const U8 marker = data[pos + 1];
        if (marker == SIZ)
        {
            if (body_size < SIZ_MIN_BODY)
            {
                return {};
            }
            const U64 x = read32(body + 2);
            const U64 y = read32(body + 6);
            const U64 x_origin = read32(body + 10);
            const U64 y_origin = read32(body + 14);
            const U64 tile_width = read32(body + 18);
            const U64 tile_height = read32(body + 22);
            const U64 tile_x_origin = read32(body + 26);
            const U64 tile_y_origin = read32(body + 30);
            if (x <= x_origin || y <= y_origin || x - x_origin > U16_MAX || y - y_origin > U16_MAX
                || !tile_width || !tile_height || x <= tile_x_origin || y <= tile_y_origin)
            {
                return {};
            }
            const U64 tiles = ceilDiv(x - tile_x_origin, tile_width) * ceilDiv(y - tile_y_origin, tile_height);
            layout.mWidth = S32(x - x_origin);
            layout.mHeight = S32(y - y_origin);
            layout.mComponents = S32(read16(body + 34));
            layout.mTiles = S32(llmin(tiles, U64(S32_MAX)));
            have_siz = true;
        }
        else if (marker == COD)
        {
            if (body_size < COD_MIN_BODY)
            {
                return {};
            }
            layout.mProgression = body[1];
            layout.mLayers = S32(read16(body + 2));
            layout.mLevels = body[5];
            have_cod = true;
        }
        pos += 2 + length;
    }

    if (!have_siz || !have_cod || pos + SOT_FIELDS_SIZE > size || data[pos] != MARKER || data[pos + 1] != SOT)
    {
        return {};
    }

    layout.mHeaderLength = pos;

    const U32 tile_part_length = read32(data + pos + 6);
    const U8 tile_parts = data[pos + 11];
    if (layout.mTiles == 1 && tile_parts == 1 && tile_part_length >= MIN_TILE_PART_SIZE
        && tile_part_length <= U32(MAX_IMAGE_DATA_SIZE))
    {
        layout.mLength = pos + S32(tile_part_length) + EOC_SIZE;
    }
    return layout;
}

S32 ALJ2CLayout::calcDataSize(S32 discard) const
{
    if (!isValid())
    {
        return 0;
    }
    discard = llclamp(discard, 0, MAX_DISCARD_LEVEL);
    const S32 bytes = S32(F64(mLength) * streamingFraction(llmax(mWidth, mHeight), discard));
    return llclamp(bytes, llmin(FIRST_PACKET_SIZE, mLength), mLength);
}

S32 ALJ2CLayout::calcDiscardLevelBytes(S32 bytes) const
{
    if (!isValid())
    {
        return MAX_DISCARD_LEVEL;
    }
    for (S32 discard = 0; discard < MAX_DISCARD_LEVEL; ++discard)
    {
        if (bytes >= calcDataSize(discard))
        {
            return discard;
        }
    }
    return MAX_DISCARD_LEVEL;
}
