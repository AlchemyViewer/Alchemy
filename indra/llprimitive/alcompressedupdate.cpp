/**
 * @file alcompressedupdate.cpp
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

#include "linden_common.h"

#include "alcompressedupdate.h"

#include "lldatapacker.h"
#include "llvolumemessage.h"

bool ALCompressedSoundAndNames::unpack(LLDataPacker& dp, U32 flags)
{
    *this = ALCompressedSoundAndNames();
    if (flags & 0x10)
    {
        if (!dp.unpackUUID(mSoundID, "SoundUUID")
            || !dp.unpackF32(mSoundGain, "SoundGain")
            || !dp.unpackU8(mSoundFlags, "SoundFlags")
            || !dp.unpackF32(mSoundRadius, "SoundRadius"))
        {
            return false;
        }
    }
    if (flags & 0x100)
    {
        if (!dp.unpackString(mNameValues, "NV"))
        {
            return false;
        }
    }
    return true;
}

ALCompressedVolume::ECut ALCompressedVolume::unpack(LLDataPacker& dp, U32 flags)
{
    // A volume's parameters take a run of bytes of one size: it is read
    // whole, and the parameters unpacked from what was read.
    U8 params[LLVolumeMessage::PACKED_VOLUME_PARAMS_SIZE];
    if (!dp.unpackBinaryDataFixed(params, sizeof(params), "VolumeParams"))
    {
        return ECut::VOLUME_PARAMS;
    }
    LLDataPackerBinaryBuffer params_dp(params, sizeof(params));
    mVolumeParamsInRange = LLVolumeMessage::unpackVolumeParams(&mVolumeParams, params_dp);

    if (!dp.unpackBinaryData(mTextureEntry, "TextureEntry"))
    {
        return ECut::TEXTURE_ENTRY;
    }

    mHasTextureAnim = (flags & 0x40) != 0;
    if (mHasTextureAnim && !mTextureAnim.unpackTAMessage(dp))
    {
        return ECut::TEXTURE_ANIM;
    }
    return ECut::NONE;
}
