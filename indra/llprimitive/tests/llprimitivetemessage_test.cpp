/**
 * @file llprimitivetemessage_test.cpp
 * @brief LLPrimitive's texture entry packers, on real texture entries.
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

#include "../llprimitive.h"

#include "lldatapacker.h"
#include "llmaterialid.h"
#include "v4color.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

namespace
{
    constexpr U8 MAX_TES = (U8)LLTEContents::MAX_TES;

    LLUUID face_uuid(U8 face, U8 salt)
    {
        LLUUID id;
        for (U8 i = 0; i < UUID_BYTES; ++i)
        {
            id.mData[i] = (U8)(face * 5 + i + salt);
        }
        return id;
    }

    // Gives each face texture entry values of its own in every field a
    // texture entry packs: all but the media byte, whose setters keep three
    // bits of it.
    void give_faces_their_own_entries(LLPrimitive& prim, U8 faces)
    {
        prim.setNumTEs(faces);
        for (U8 face = 0; face < faces; ++face)
        {
            const F32 step = (F32)face;
            prim.setTETexture(face, face_uuid(face, 1));
            prim.setTEColor(face, LLColor4((face + 1) / 255.f, (face + 2) / 255.f, (face + 3) / 255.f, (face + 4) / 255.f));
            prim.setTEScale(face, 1.f + step * 0.25f, 2.f + step * 0.5f);
            prim.setTEOffset(face, -0.5f + step / 64.f, 0.25f - step / 128.f);
            prim.setTERotation(face, step * 0.1f);
            prim.setTEBumpShinyFullbright(face, face);
            prim.setTEMediaTexGen(face, face & 7);
            prim.setTEGlow(face, (face + 1) / 255.f);
            prim.setTEMaterialID(face, LLMaterialID(face_uuid(face, 100)));
        }
    }

    // The texture entry a data packer packs, without the size before it.
    std::vector<U8> packed_entry(const LLPrimitive& prim)
    {
        std::vector<U8> buffer(16384);
        LLDataPackerBinaryBuffer writer(buffer.data(), (S32)buffer.size());
        REQUIRE(prim.packTEMessage(writer));
        S32 size = 0;
        memcpy(&size, buffer.data(), sizeof(size));
        REQUIRE(size == writer.getCurrentSize() - 4);
        return std::vector<U8>(buffer.begin() + 4, buffer.begin() + 4 + size);
    }

    // An entry as a data packer packs it: its size, then its bytes.
    std::vector<U8> sized(const std::vector<U8>& entry)
    {
        std::vector<U8> buffer(4 + entry.size());
        const S32 size = (S32)entry.size();
        memcpy(buffer.data(), &size, sizeof(size));
        if (!entry.empty())
        {
            memcpy(buffer.data() + 4, entry.data(), entry.size());
        }
        return buffer;
    }

    TEST_CASE("LLPrimitiveTEMessage: an entry round-trips through a data packer", "[LLPrimitiveTEMessage]")
    {
        LLPrimitive prim;
        give_faces_their_own_entries(prim, MAX_TES);
        const std::vector<U8> entry = packed_entry(prim);

        std::vector<U8> buffer = sized(entry);
        LLPrimitive copy;
        copy.setNumTEs(MAX_TES);
        LLDataPackerBinaryBuffer reader(buffer.data(), (S32)buffer.size());
        CHECK(copy.unpackTEMessage(reader) != TEM_INVALID);
        CHECK(packed_entry(copy) == entry);
        CHECK(copy.getTE(MAX_TES - 1)->getMaterialID() == LLMaterialID(face_uuid(MAX_TES - 1, 100)));
    }

    TEST_CASE("LLPrimitiveTEMessage: a data packer packs no more than forty-five faces", "[LLPrimitiveTEMessage]")
    {
        LLPrimitive first;
        give_faces_their_own_entries(first, MAX_TES);
        LLPrimitive many;
        give_faces_their_own_entries(many, 200);
        CHECK(packed_entry(many) == packed_entry(first));
    }

    // Overrides of a face with the value it already has lengthen an entry
    // without changing what it says. Repeated in its first field, they move
    // its last field, the material IDs, past where a 4096-byte buffer ends.
    TEST_CASE("LLPrimitiveTEMessage: a data packer reads an entry of any length", "[LLPrimitiveTEMessage]")
    {
        LLPrimitive prim;
        give_faces_their_own_entries(prim, MAX_TES);
        const std::vector<U8> entry = packed_entry(prim);

        std::vector<U8> override_face_0(1 + UUID_BYTES);
        override_face_0[0] = 0x01;
        memcpy(&override_face_0[1], face_uuid(0, 1).mData, UUID_BYTES);
        std::vector<U8> longer(entry.begin(), entry.begin() + UUID_BYTES);
        while (longer.size() + entry.size() < 6000)
        {
            longer.insert(longer.end(), override_face_0.begin(), override_face_0.end());
        }
        longer.insert(longer.end(), entry.begin() + UUID_BYTES, entry.end());
        REQUIRE(longer.size() > 4096 + 1000);

        std::vector<U8> buffer = sized(longer);
        LLPrimitive copy;
        copy.setNumTEs(MAX_TES);
        LLDataPackerBinaryBuffer reader(buffer.data(), (S32)buffer.size());
        CHECK(copy.unpackTEMessage(reader) != TEM_INVALID);
        CHECK(packed_entry(copy) == entry);
        CHECK(copy.getTE(MAX_TES - 1)->getMaterialID() == LLMaterialID(face_uuid(MAX_TES - 1, 100)));
    }

    // An entry whose size runs past what its data packer holds, as a field
    // filled partway would give, is refused whole.
    TEST_CASE("LLPrimitiveTEMessage: an entry past the end of its data packer is refused and changes no face", "[LLPrimitiveTEMessage]")
    {
        LLPrimitive prim;
        give_faces_their_own_entries(prim, 8);
        std::vector<U8> buffer = sized(packed_entry(prim));

        LLPrimitive copy;
        copy.setNumTEs(8);
        const std::vector<U8> before = packed_entry(copy);
        LLDataPackerBinaryBuffer reader(buffer.data(), (S32)buffer.size() / 2);
        CHECK(copy.unpackTEMessage(reader) == TEM_INVALID);
        CHECK(packed_entry(copy) == before);
    }
}
