/**
 * @file alj2clayout.h
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

#pragma once

#include "stdtypes.h"

// What a JPEG 2000 codestream's main header (SIZ, COD) and first tile-part header (SOT) say
// about it, and the bytes from its start that a decode at each discard level needs.
//
// The total length is known only for a codestream of one tile in one tile-part, the shape
// every Second Life texture has: the first SOT's Psot then runs to the EOC.
struct ALJ2CLayout
{
    enum EProgression : S32
    {
        LRCP = 0,
        RLCP = 1,
        RPCL = 2,
        PCRL = 3,
        CPRL = 4,
    };

    S32 mWidth = 0;
    S32 mHeight = 0;
    S32 mComponents = 0;
    S32 mTiles = 0;
    S32 mProgression = -1;
    S32 mLayers = 0;
    // Decomposition levels; the codestream holds mLevels + 1 resolutions.
    S32 mLevels = 0;
    // Offset of the first SOT marker, which ends the main header.
    S32 mHeaderLength = 0;
    // The whole codestream, EOC included. 0 when the headers do not fix it.
    S32 mLength = 0;

    // Reads a codestream's headers from its first bytes. The layout is invalid when they
    // stop short of the first SOT's fields or are not a codestream.
    static ALJ2CLayout parse(const U8* data, S32 size);

    bool isValid() const { return mLength > 0; }

    // Bytes from the start of the codestream that decode at `discard` within 45 dB PSNR of
    // a decode at that level from the whole of it.
    S32 calcDataSize(S32 discard) const;

    // The finest discard level the first `bytes` of the codestream decode.
    S32 calcDiscardLevelBytes(S32 bytes) const;
};
