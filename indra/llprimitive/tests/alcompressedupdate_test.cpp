/**
 * @file alcompressedupdate_test.cpp
 * @brief Tests for the fields of a compressed object update after its extra parameters
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

#include "../alcompressedupdate.h"
#include "../llvolumemessage.h"

#include "lldatapacker.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    // A compressed update's sound and name values, as a simulator packs them
    // where flags hold them.
    std::vector<U8> pack_sound_and_names(const ALCompressedSoundAndNames& fields, U32 flags)
    {
        std::vector<U8> buffer(256);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        if (flags & 0x10)
        {
            REQUIRE(dp.packUUID(fields.mSoundID, "SoundUUID"));
            REQUIRE(dp.packF32(fields.mSoundGain, "SoundGain"));
            REQUIRE(dp.packU8(fields.mSoundFlags, "SoundFlags"));
            REQUIRE(dp.packF32(fields.mSoundRadius, "SoundRadius"));
        }
        if (flags & 0x100)
        {
            REQUIRE(dp.packString(fields.mNameValues, "NV"));
        }
        buffer.resize(dp.getCurrentSize());
        return buffer;
    }

    ALCompressedSoundAndNames some_sound_and_names()
    {
        ALCompressedSoundAndNames fields;
        fields.mSoundID.generate();
        fields.mSoundGain = 0.5f;
        fields.mSoundFlags = 3;
        fields.mSoundRadius = 20.f;
        fields.mNameValues = "AttachItemID STRING RW SV c3c1b8f4-1b54-4b05-9c47-8b7a3fe2a3f1";
        return fields;
    }

    TEST_CASE("ALCompressedSoundAndNames: an update's sound and name values are read whole", "[ALCompressedSoundAndNames]")
    {
        const ALCompressedSoundAndNames sent = some_sound_and_names();
        std::vector<U8> buffer = pack_sound_and_names(sent, 0x110);

        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        ALCompressedSoundAndNames read;
        REQUIRE(read.unpack(dp, 0x110));
        CHECK(read.mSoundID == sent.mSoundID);
        CHECK(read.mSoundGain == sent.mSoundGain);
        CHECK(read.mSoundFlags == sent.mSoundFlags);
        CHECK(read.mSoundRadius == sent.mSoundRadius);
        CHECK(read.mNameValues == sent.mNameValues);
        CHECK_FALSE(dp.hasNext());

        UNSCOPED_INFO("an update that holds neither reads no sound and no name values");
        LLDataPackerBinaryBuffer neither(buffer.data(), (S32)buffer.size());
        REQUIRE(read.unpack(neither, 0));
        CHECK(read.mSoundID.isNull());
        CHECK(read.mSoundGain == 0.f);
        CHECK(read.mNameValues.empty());
        CHECK(neither.getCurrentSize() == 0);
    }

    TEST_CASE("ALCompressedSoundAndNames: an update cut short in its sound or its name values is refused", "[ALCompressedSoundAndNames]")
    {
        const ALCompressedSoundAndNames sent = some_sound_and_names();
        ALCompressedSoundAndNames read;
        for (U32 flags : { 0x10u, 0x100u, 0x110u })
        {
            std::vector<U8> buffer = pack_sound_and_names(sent, flags);
            for (S32 length = 0; length < (S32)buffer.size(); ++length)
            {
                CAPTURE(flags, length);
                LLDataPackerBinaryBuffer cut(buffer.data(), length);
                CHECK_FALSE(read.unpack(cut, flags));
            }
        }
    }

    // A volume's fields of a compressed update as a simulator packs them,
    // with a byte of the particle system that follows them after them.
    struct PackedVolume
    {
        std::vector<U8> mBytes;
        S32 mParamsEnd = 0;
        S32 mTextureEntryEnd = 0;
        S32 mTextureAnimEnd = 0;
    };

    PackedVolume pack_volume(const LLVolumeParams& params, const LLPrimitive& faces, const LLTextureAnim& anim)
    {
        PackedVolume packed;
        packed.mBytes.resize(4096);
        LLDataPackerBinaryBuffer dp(packed.mBytes.data(), (S32)packed.mBytes.size());
        REQUIRE(LLVolumeMessage::packVolumeParams(&params, dp));
        packed.mParamsEnd = dp.getCurrentSize();
        REQUIRE(faces.packTEMessage(dp));
        packed.mTextureEntryEnd = dp.getCurrentSize();
        anim.packTAMessage(dp);
        packed.mTextureAnimEnd = dp.getCurrentSize();
        REQUIRE(dp.packU8(0xab, "PartSys"));
        packed.mBytes.resize(dp.getCurrentSize());
        return packed;
    }

    LLTextureAnim some_animation()
    {
        LLTextureAnim anim;
        anim.mMode = LLTextureAnim::ON | LLTextureAnim::LOOP;
        anim.mFace = 1;
        anim.mSizeX = 4;
        anim.mSizeY = 2;
        anim.mStart = 0.f;
        anim.mLength = 8.f;
        anim.mRate = 8.f;
        return anim;
    }

    TEST_CASE("ALCompressedVolume: a volume's parameters take as many bytes as the update reads for them", "[ALCompressedVolume]")
    {
        LLVolumeParams params;
        params.setHollow(0.5f);
        LLDataPackerBinaryBuffer counter;
        REQUIRE(LLVolumeMessage::packVolumeParams(&params, counter));
        CHECK(counter.getCurrentSize() == LLVolumeMessage::PACKED_VOLUME_PARAMS_SIZE);
    }

    TEST_CASE("ALCompressedVolume: a volume's fields are read whole", "[ALCompressedVolume]")
    {
        LLVolumeParams params;
        params.setHollow(0.5f);
        LLPrimitive faces;
        faces.setNumTEs(3);
        LLUUID texture;
        texture.generate();
        faces.setTETexture(1, texture);
        const LLTextureAnim anim = some_animation();
        const PackedVolume packed = pack_volume(params, faces, anim);

        // What the parameters read as from the update alone.
        std::vector<U8> bytes = packed.mBytes;
        LLVolumeParams expected;
        LLDataPackerBinaryBuffer params_dp(bytes.data(), packed.mParamsEnd);
        const bool in_range = LLVolumeMessage::unpackVolumeParams(&expected, params_dp);

        LLDataPackerBinaryBuffer dp(bytes.data(), (S32)bytes.size());
        ALCompressedVolume read;
        REQUIRE(read.unpack(dp, 0x40) == ALCompressedVolume::ECut::NONE);
        CHECK(read.mVolumeParams == expected);
        CHECK(read.mVolumeParamsInRange == in_range);
        CHECK(read.mHasTextureAnim);
        CHECK(read.mTextureAnim.equals(anim));
        UNSCOPED_INFO("the particle system after them is left to read");
        CHECK(dp.getCurrentSize() == packed.mTextureAnimEnd);

        LLPrimitive applied;
        applied.setNumTEs(3);
        applied.unpackTEEntry(read.mTextureEntry);
        REQUIRE(applied.getTE(1) != nullptr);
        CHECK(applied.getTE(1)->getID() == texture);

        UNSCOPED_INFO("an update with no animation reads none");
        LLDataPackerBinaryBuffer no_anim(bytes.data(), (S32)bytes.size());
        REQUIRE(read.unpack(no_anim, 0) == ALCompressedVolume::ECut::NONE);
        CHECK_FALSE(read.mHasTextureAnim);
        CHECK(no_anim.getCurrentSize() == packed.mTextureEntryEnd);
    }

    // The viewer flushes the region's cache where an update ends before the
    // end of its texture entry, and refuses the update wherever it ends.
    TEST_CASE("ALCompressedVolume: an update cut short says which field it ends in", "[ALCompressedVolume]")
    {
        LLVolumeParams params;
        LLPrimitive faces;
        faces.setNumTEs(2);
        const PackedVolume packed = pack_volume(params, faces, some_animation());
        std::vector<U8> bytes = packed.mBytes;

        ALCompressedVolume read;
        for (S32 length = 0; length < packed.mTextureAnimEnd; ++length)
        {
            CAPTURE(length);
            LLDataPackerBinaryBuffer cut(bytes.data(), length);
            const ALCompressedVolume::ECut expected = length < packed.mParamsEnd ? ALCompressedVolume::ECut::VOLUME_PARAMS
                                                      : length < packed.mTextureEntryEnd ? ALCompressedVolume::ECut::TEXTURE_ENTRY
                                                                                         : ALCompressedVolume::ECut::TEXTURE_ANIM;
            CHECK(read.unpack(cut, 0x40) == expected);
        }
    }

    TEST_CASE("LLTextureAnim: unpackTAMessage refuses a block past the end of the data", "[LLTextureAnim]")
    {
        const LLTextureAnim anim = some_animation();
        std::vector<U8> buffer(64);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        anim.packTAMessage(dp);
        const S32 size = dp.getCurrentSize();

        LLTextureAnim read;
        read.mMode = LLTextureAnim::LOOP;
        LLDataPackerBinaryBuffer cut(buffer.data(), size - 1);
        CHECK_FALSE(read.unpackTAMessage(cut));
        CHECK(read.mMode == 0);

        LLDataPackerBinaryBuffer whole(buffer.data(), size);
        CHECK(read.unpackTAMessage(whole));
        CHECK(read.equals(anim));

        UNSCOPED_INFO("a block of another size is read past and leaves the animation off");
        U8 other[] = { 4, 0, 0, 0, 1, 2, 3, 4 };
        LLDataPackerBinaryBuffer other_dp(other, sizeof(other));
        CHECK(read.unpackTAMessage(other_dp));
        CHECK(read.mMode == 0);
        CHECK_FALSE(other_dp.hasNext());
    }
}
