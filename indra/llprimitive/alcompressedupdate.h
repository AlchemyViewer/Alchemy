/**
 * @file alcompressedupdate.h
 * @brief The fields of a compressed object update after its extra parameters
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

#include "llprimitive.h"
#include "lltextureanim.h"
#include "lluuid.h"
#include "llvolume.h"

#include <string>

class LLDataPacker;

// The fields of a compressed object update after its extra parameters, each
// group read whole before any of it is applied. The fields sit in one stream,
// so an update cut short in one holds nothing after it to read, and is
// refused, as one cut short in its extra parameters is.

// The fields there that every object's update holds: its attached sound,
// under the update's 0x10, and its name values, under 0x100. Where the
// update holds neither, they are as here: no sound, and no name values.
struct ALCompressedSoundAndNames
{
    // False where one runs past the end of the update.
    bool unpack(LLDataPacker& dp, U32 flags);

    LLUUID      mSoundID;
    F32         mSoundGain = 0.f;
    U8          mSoundFlags = 0;
    F32         mSoundRadius = 0.f;
    std::string mNameValues;
};

// What a volume's update holds after those: the volume's parameters, its
// texture entry, and its texture animation, under the update's 0x40. The
// particle system after them, under 0x400, is not one of them: one cut short
// reads as no system.
struct ALCompressedVolume
{
    // Where the fields run past the end of the update: nowhere, or in the
    // first of them that does.
    enum class ECut { NONE, VOLUME_PARAMS, TEXTURE_ENTRY, TEXTURE_ANIM };
    ECut unpack(LLDataPacker& dp, U32 flags);

    LLVolumeParams mVolumeParams;
    // Whether the parameters were in range, as unpackVolumeParams says.
    bool mVolumeParamsInRange = false;
    // As LLPrimitive::unpackTEEntry applies it.
    LLPrimitive::te_bytes_t mTextureEntry;
    bool mHasTextureAnim = false;
    LLTextureAnim mTextureAnim;
};
