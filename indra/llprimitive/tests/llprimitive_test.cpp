/**
 * @file llprimitive_test.cpp
 * @brief llprimitive tests
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "../llprimitive.h"

#include "../../llmath/llvolumemgr.h"
#include "../../llmessage/lldatapacker.h"

namespace
{
class DummyVolumeMgr : public LLVolumeMgr
{
public:
    DummyVolumeMgr() : LLVolumeMgr(), mVolumeTest(NULL), mCurrDetailTest(0) {}
    ~DummyVolumeMgr()
    {
    }


    virtual LLVolume *refVolume(const LLVolumeParams &volume_params, const S32 detail)
    {
        if (mVolumeTest.isNull() || volume_params != mCurrParamsTest || detail != mCurrDetailTest)
        {
            F32 volume_detail = LLVolumeLODGroup::getVolumeScaleFromDetail(detail);
            mVolumeTest = new LLVolume(volume_params, volume_detail, false, false);
            mCurrParamsTest = volume_params;
            mCurrDetailTest = detail;
            return mVolumeTest;
        }
        else
        {
            return mVolumeTest;
        }
    }

    virtual void unrefVolume(LLVolume *volumep)
    {
        if (mVolumeTest == volumep)
        {
            mVolumeTest = NULL;
        }
    }

private:
    LLPointer<LLVolume> mVolumeTest;
    LLVolumeParams mCurrParamsTest;
    S32 mCurrDetailTest;
};
}

LLMaterialID::LLMaterialID() = default;
void LLMaterialID::set(void const*) { }
U8 const * LLMaterialID::get() const { return mID; }

LLPrimTextureList::LLPrimTextureList() { }
LLPrimTextureList::~LLPrimTextureList() { }
S32 LLPrimTextureList::setBumpMap(const U8 index, const U8 bump) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setOffsetS(const U8 index, const F32 s) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setOffsetT(const U8 index, const F32 t) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::copyTexture(const U8 index, const LLTextureEntry &te) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setRotation(const U8 index, const F32 r) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setBumpShiny(const U8 index, const U8 bump_shiny) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setFullbright(const U8 index, const U8 t) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setMaterialID(const U8 index, const LLMaterialID& pMaterialID) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setMediaFlags(const U8 index, const U8 media_flags) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setMediaTexGen(const U8 index, const U8 media) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setMaterialParams(const U8 index, const LLMaterialPtr pMaterialParams) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setBumpShinyFullbright(const U8 index, const U8 bump) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setID(const U8 index, const LLUUID& id) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setGlow(const U8 index, const F32 glow) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setAlpha(const U8 index, const F32 alpha) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setColor(const U8 index, const LLColor3& color) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setColor(const U8 index, const LLColor4& color) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setScale(const U8 index, const F32 s, const F32 t) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setScaleS(const U8 index, const F32 s) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setScaleT(const U8 index, const F32 t) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setShiny(const U8 index, const U8 shiny) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setOffset(const U8 index, const F32 s, const F32 t) { return TEM_CHANGE_NONE; }
S32 LLPrimTextureList::setTexGen(const U8 index, const U8 texgen) { return TEM_CHANGE_NONE; }

LLMaterialPtr LLPrimTextureList::getMaterialParams(const U8 index) { return LLMaterialPtr(); }
void LLPrimTextureList::copy(LLPrimTextureList const & ptl) { mEntryList = ptl.mEntryList; } // do we need to call getTexture()->newCopy()?
void LLPrimTextureList::take(LLPrimTextureList &other_list) { }
void LLPrimTextureList::setSize(S32 new_size) { mEntryList.resize(new_size); }
void LLPrimTextureList::setAllIDs(const LLUUID &id) { }
LLTextureEntry * LLPrimTextureList::getTexture(const U8 index) const { return nullptr; }
S32 LLPrimTextureList::size() const { return static_cast<S32>(mEntryList.size()); }

namespace
{
class PRIMITIVE_TEST_SETUP
{
public:
    PRIMITIVE_TEST_SETUP()
    {
        volume_manager_test = new DummyVolumeMgr();
        LLPrimitive::setVolumeManager(volume_manager_test);
    }

    ~PRIMITIVE_TEST_SETUP()
    {
        LLPrimitive::cleanupVolumeManager();
    }
    DummyVolumeMgr * volume_manager_test;
};
}

namespace
{
    struct llprimitive
    {
        PRIMITIVE_TEST_SETUP setup_class;
    };


    TEST_CASE_METHOD(llprimitive, "LLPrimitive: a default primitive constructs and destroys cleanly", "[LLPrimitive]")
    {
        LLPrimitive test;
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: getPCode starts at zero and returns what setPCode set", "[LLPrimitive]")
    {
        LLPrimitive test;
        CHECK(test.getPCode() == 0);
        LLPCode code = 1;
        test.setPCode(code);
        CHECK(test.getPCode() == code);
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: init_primitive sets the PCode", "[LLPrimitive]")
    {
        LLPCode code = 1;
        LLPrimitive primitive;
        primitive.init_primitive(code);
        CHECK(primitive.getPCode() == code);
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: createPrimitive makes a primitive with the PCode", "[LLPrimitive]")
    {
        LLPCode code = 1;
        const std::unique_ptr<LLPrimitive> primitive(LLPrimitive::createPrimitive(code));
        REQUIRE(primitive != nullptr);
        CHECK(primitive->getPCode() == code);
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: setVolume of a unique volume replaces it only when params change", "[LLPrimitive]")
    {
        LLPrimitive primitive;
        LLVolumeParams params;

        // Make sure volume starts off null
        CHECK(primitive.getVolume() == NULL);

        // Make sure we have no texture entries before setting the volume
        CHECK(primitive.getNumTEs() == 0);

        // Test that GEOMETRY has not been flagged as changed.
        CHECK_FALSE(primitive.isChanged(LLXform::GEOMETRY));

        // Make sure setVolume returns true
        CHECK(primitive.setVolume(params, 0, true));
        LLVolume* new_volume = primitive.getVolume();

        // make sure new volume was actually created
        REQUIRE(new_volume != NULL);

        // Make sure that now that we've set the volume we have texture entries
        CHECK(primitive.getNumTEs() != 0);

        // Make sure that the number of texture entries equals the number of faces in the volume (should be 6)
        CHECK(new_volume->getNumFaces() == 6);
        CHECK(primitive.getNumTEs() == new_volume->getNumFaces());

        // Test that GEOMETRY has been flagged as changed.
        CHECK(primitive.isChanged(LLXform::GEOMETRY));

        // Run it twice to make sure it doesn't create a different one if params are the same
        CHECK_FALSE(primitive.setVolume(params, 0, true));
        CHECK(new_volume == primitive.getVolume());

        // Change the param definition and try setting it again.
        params.setRevolutions(4);
        CHECK(primitive.setVolume(params, 0, true));

        // Ensure that we now have a different volume
        CHECK(new_volume != primitive.getVolume());
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: setVolume of a shared volume replaces it only when params change", "[LLPrimitive]")
    {
        LLPrimitive primitive;
        LLVolumeParams params;

        // Make sure volume starts off null
        CHECK(primitive.getVolume() == NULL);

        // Make sure we have no texture entries before setting the volume
        CHECK(primitive.getNumTEs() == 0);

        // Test that GEOMETRY has not been flagged as changed.
        CHECK_FALSE(primitive.isChanged(LLXform::GEOMETRY));

        // Make sure setVolume returns true
        CHECK(primitive.setVolume(params, 0, false));

        LLVolume* new_volume = primitive.getVolume();

        // make sure new volume was actually created
        REQUIRE(new_volume != NULL);

        // Make sure that now that we've set the volume we have texture entries
        CHECK(primitive.getNumTEs() != 0);

        // Make sure that the number of texture entries equals the number of faces in the volume (should be 6)
        CHECK(new_volume->getNumFaces() == 6);
        CHECK(primitive.getNumTEs() == new_volume->getNumFaces());

        // Test that GEOMETRY has been flagged as changed.
        CHECK(primitive.isChanged(LLXform::GEOMETRY));

        // Run it twice to make sure it doesn't create a different one if params are the same
        CHECK_FALSE(primitive.setVolume(params, 0, false));
        CHECK(new_volume == primitive.getVolume());

        // Change the param definition and try setting it again.
        params.setRevolutions(4);
        CHECK(primitive.setVolume(params, 0, false));

        // Ensure that we now have a different volume
        CHECK(new_volume != primitive.getVolume());
    }

    // As LLViewerObject::setParameterEntry compares a block it is given with the one it holds.
    bool same_block(const LLNetworkData& given, const LLNetworkData& held)
    {
        return given == held;
    }

    TEST_CASE("LLLightParams: a light's color comes back from the wire as it is held", "[LLLightParams]")
    {
        LLLightParams sent;
        sent.setLinearColor(LLColor4(0.31f, 0.02f, 0.9f, 0.43f));

        U8 buffer[64];
        LLDataPackerBinaryBuffer out(buffer, sizeof(buffer));
        REQUIRE(sent.pack(out));

        LLLightParams received;
        LLDataPackerBinaryBuffer in(buffer, out.getCurrentSize());
        REQUIRE(received.unpack(in));

        CHECK(received.getLinearColor() == sent.getLinearColor());
        CHECK(same_block(received, sent));
    }

    TEST_CASE("LLLightParams: colors that pack to the same bytes are the same light", "[LLLightParams]")
    {
        LLLightParams light;
        light.setLinearColor(LLColor4(0.31f, 0.6f, 0.2f, 1.f));
        LLLightParams same;
        same.setLinearColor(LLColor4(0.311f, 0.6f, 0.2f, 1.f));
        LLLightParams next;
        next.setLinearColor(LLColor4(0.32f, 0.6f, 0.2f, 1.f));

        CHECK(same_block(same, light));
        CHECK_FALSE(same_block(next, light));
    }

    TEST_CASE("LLLightParams: a color read back in sRGB and set again holds the same", "[LLLightParams]")
    {
        for (S32 code = 0; code < 256; ++code)
        {
            const F32 channel = code / 255.f;
            LLLightParams held;
            held.setLinearColor(LLColor4(channel, channel, channel, channel));
            LLLightParams again;
            again.setSRGBColor(held.getSRGBColor());

            CAPTURE(code);
            CHECK(same_block(again, held));
        }
    }

    TEST_CASE("LLLightParams: a color out of range is held at the ends of the range", "[LLLightParams]")
    {
        LLLightParams light;
        light.setLinearColor(LLColor4(1e30f, -1.f, 0.f, 3.f));

        CHECK(light.getLinearColor() == LLColor4(1.f, 0.f, 0.f, 1.f));
    }

    TEST_CASE_METHOD(llprimitive, "LLPrimitive: setVolume keeps an LOD-invariant shared volume at every LOD", "[LLPrimitive]")
    {
        // A box is the same at every LOD and keeps the volume it has.
        LLPrimitive box;
        LLVolumeParams box_params;
        REQUIRE(LLVolume::isLODInvariant(box_params));
        CHECK(box.setVolume(box_params, 0, false));
        LLVolume* box_volume = box.getVolume();
        REQUIRE(box_volume != NULL);
        for (S32 lod : { 1, 2, 3, 0 })
        {
            CHECK_FALSE(box.setVolume(box_params, lod, false));
            CHECK(box.getVolume() == box_volume);
        }

        // A cylinder takes the volume of each LOD it asks for.
        LLPrimitive cylinder;
        LLVolumeParams cylinder_params;
        cylinder_params.setType(LL_PCODE_PROFILE_CIRCLE, LL_PCODE_PATH_LINE);
        REQUIRE_FALSE(LLVolume::isLODInvariant(cylinder_params));
        CHECK(cylinder.setVolume(cylinder_params, 0, false));
        for (S32 lod : { 1, 2, 3 })
        {
            CHECK(cylinder.setVolume(cylinder_params, lod, false));
            CHECK(cylinder.getVolume()->getDetail() == LLVolumeLODGroup::getVolumeScaleFromDetail(lod));
        }
    }

    TEST_CASE("LLNetworkData: each extra parameter type has a slot of its own", "[LLNetworkData]")
    {
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_FLEXIBLE) == 0);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_LIGHT) == 1);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_SCULPT) == 2);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_LIGHT_IMAGE) == 3);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_RESERVED) == 4);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_MESH) == 5);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_EXTENDED_MESH) == 6);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_RENDER_MATERIAL) == 7);
        CHECK(LLNetworkData::slotOf(LLNetworkData::PARAMS_REFLECTION_PROBE) == 8);
        CHECK(LLNetworkData::SLOT_COUNT == 9);
    }

    // A type under the first, past the last or between two has no slot,
    // rather than one before the slots or another type's.
    TEST_CASE("LLNetworkData: a type that is not one of the multiples of 0x10 up to the last has no slot", "[LLNetworkData]")
    {
        for (U32 type = 0; type <= 0xFFFF; ++type)
        {
            const S32 slot = LLNetworkData::slotOf((U16)type);
            const bool has_slot = type >= LLNetworkData::PARAMS_FLEXIBLE && type <= LLNetworkData::PARAMS_MAX && (type & 0xF) == 0;
            CAPTURE(type);
            if (has_slot)
            {
                CHECK(slot == (S32)(type >> 4) - 1);
            }
            else if (slot != -1)
            {
                FAIL_CHECK("a slot for a type with none");
            }
        }
    }

    // An extra parameter as a test sends it.
    struct SentEntry
    {
        U16 mType = 0;
        std::vector<U8> mData;
    };

    // Packs extra parameters as an object update holds them: their count,
    // then each one's type and data.
    std::vector<U8> pack_entries(const std::vector<SentEntry>& entries)
    {
        std::vector<U8> buffer(65536);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        REQUIRE(dp.packU8((U8)entries.size(), "num_params"));
        for (const SentEntry& entry : entries)
        {
            REQUIRE(dp.packU16(entry.mType, "param_type"));
            REQUIRE(dp.packBinaryData(entry.mData.data(), (S32)entry.mData.size(), "param_data"));
        }
        buffer.resize(dp.getCurrentSize());
        return buffer;
    }

    bool is_valid(U16 type, const std::vector<U8>& data)
    {
        return LLNetworkData::isValid(type, data.data(), data.size());
    }

    // A render material's data: its count, then a face and a material ID for
    // each.
    std::vector<U8> render_material_data(U8 count)
    {
        std::vector<U8> data(1 + 17 * (size_t)count);
        data[0] = count;
        for (U8 i = 0; i < count; ++i)
        {
            U8* entry = &data[1 + 17 * (size_t)i];
            entry[0] = i;
            for (U8 b = 0; b < 16; ++b)
            {
                entry[1 + b] = (U8)(i + b * 3 + 1);
            }
        }
        return data;
    }

    TEST_CASE("LLNetworkData: unpackEntries reads every entry an update holds whole", "[LLNetworkData]")
    {
        std::vector<SentEntry> sent(3);
        sent[0].mType = LLNetworkData::PARAMS_LIGHT;
        sent[0].mData.assign(16, 0x11);
        sent[1].mType = LLNetworkData::PARAMS_RENDER_MATERIAL;
        sent[1].mData = render_material_data(70);
        sent[2].mType = LLNetworkData::PARAMS_EXTENDED_MESH;
        sent[2].mData.assign(4, 0x22);
        REQUIRE(sent[1].mData.size() > 1024);

        std::vector<U8> buffer = pack_entries(sent);
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size());
        LLNetworkData::Entries read;
        REQUIRE(LLNetworkData::unpackEntries(dp, read));
        REQUIRE(read.mEntries.size() == sent.size());
        for (size_t i = 0; i < sent.size(); ++i)
        {
            CAPTURE(i);
            const LLNetworkData::Entries::Entry& entry = read.mEntries[i];
            CHECK(entry.mType == sent[i].mType);
            REQUIRE(entry.mOffset + entry.mSize <= read.mBytes.size());
            const U8* data = read.mBytes.data() + entry.mOffset;
            CHECK(std::vector<U8>(data, data + entry.mSize) == sent[i].mData);
        }
        CHECK_FALSE(dp.hasNext());
    }

    TEST_CASE("LLNetworkData: unpackEntries refuses an entry past the end of its block", "[LLNetworkData]")
    {
        std::vector<SentEntry> sent(2);
        sent[0].mType = LLNetworkData::PARAMS_LIGHT;
        sent[0].mData.assign(16, 0x11);
        sent[1].mType = LLNetworkData::PARAMS_SCULPT;
        sent[1].mData.assign(17, 0x33);
        std::vector<U8> buffer = pack_entries(sent);

        // Cut inside the second entry's data.
        LLDataPackerBinaryBuffer dp(buffer.data(), (S32)buffer.size() - 5);
        LLNetworkData::Entries read;
        CHECK_FALSE(LLNetworkData::unpackEntries(dp, read));
        CHECK(read.mEntries.empty());
        CHECK(read.mBytes.empty());

        // An update that ends before its count.
        LLDataPackerBinaryBuffer whole(buffer.data(), (S32)buffer.size());
        REQUIRE(LLNetworkData::unpackEntries(whole, read));
        LLDataPackerBinaryBuffer empty(buffer.data(), 0);
        CHECK_FALSE(LLNetworkData::unpackEntries(empty, read));
        CHECK(read.mEntries.empty());
        CHECK(read.mBytes.empty());
    }

    TEST_CASE("LLNetworkData: isValid holds each extra parameter to the size of its type", "[LLNetworkData]")
    {
        const auto sized = [](size_t size) { return std::vector<U8>(size, 0); };

        CHECK(is_valid(LLNetworkData::PARAMS_FLEXIBLE, sized(16)));
        CHECK(is_valid(LLNetworkData::PARAMS_FLEXIBLE, sized(4)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_FLEXIBLE, sized(15)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_FLEXIBLE, sized(0)));

        CHECK(is_valid(LLNetworkData::PARAMS_LIGHT, sized(16)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_LIGHT, sized(12)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_LIGHT, sized(17)));

        CHECK(is_valid(LLNetworkData::PARAMS_SCULPT, sized(17)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_SCULPT, sized(16)));

        CHECK(is_valid(LLNetworkData::PARAMS_LIGHT_IMAGE, sized(28)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_LIGHT_IMAGE, sized(27)));

        CHECK(is_valid(LLNetworkData::PARAMS_EXTENDED_MESH, sized(4)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_EXTENDED_MESH, sized(2)));

        CHECK(is_valid(LLNetworkData::PARAMS_REFLECTION_PROBE, sized(9)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_REFLECTION_PROBE, sized(8)));

        // A render material's count names how many faces and material IDs follow it.
        CHECK(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, render_material_data(1)));
        CHECK(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, render_material_data(200)));
        CHECK(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, render_material_data(0)));
        std::vector<U8> more_than_counted = render_material_data(3);
        more_than_counted[0] = 2;
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, more_than_counted));
        std::vector<U8> fewer_than_counted = render_material_data(3);
        fewer_than_counted[0] = 4;
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, fewer_than_counted));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, sized(0)));

        // Types with no data class of their own.
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_RESERVED, sized(16)));
        CHECK_FALSE(is_valid(LLNetworkData::PARAMS_MESH, sized(17)));
        CHECK_FALSE(is_valid(0x25, sized(16)));
    }

    typedef boost::container::small_vector<U8, 8> object_data_t;

    std::vector<U8> compressed_data(LLDataPackerBinaryBuffer& dp, U32 flags, object_data_t& data)
    {
        LLPrimitive::unpackCompressedData(dp, flags, data);
        return std::vector<U8>(data.begin(), data.end());
    }

    TEST_CASE("LLPrimitive: unpackCompressedData reads a tree's byte or a scratch pad", "[LLPrimitive]")
    {
        object_data_t data;
        const U8* inline_storage = data.data();

        U8 tree[] = { 7 };
        LLDataPackerBinaryBuffer tree_dp(tree, sizeof(tree));
        CHECK(compressed_data(tree_dp, 0x2, data) == std::vector<U8>{ 7 });
        CHECK(data.data() == inline_storage);

        // A tree's byte where the flags name both.
        LLDataPackerBinaryBuffer both_dp(tree, sizeof(tree));
        CHECK(compressed_data(both_dp, 0x3, data) == std::vector<U8>{ 7 });

        // A scratch pad's size, then its data: its own size and its bytes.
        U8 pad[] = { 99, 0, 0, 0, 3, 0, 0, 0, 1, 2, 3 };
        LLDataPackerBinaryBuffer pad_dp(pad, sizeof(pad));
        CHECK(compressed_data(pad_dp, 0x1, data) == std::vector<U8>{ 1, 2, 3 });
        CHECK_FALSE(pad_dp.hasNext());

        // Neither replaces what the data held with nothing.
        LLDataPackerBinaryBuffer none_dp(tree, sizeof(tree));
        CHECK(compressed_data(none_dp, 0x4, data).empty());
        CHECK(none_dp.hasNext());
    }

    // An update that ends before the data its flags name gives none, rather
    // than a byte that was never read.
    TEST_CASE("LLPrimitive: unpackCompressedData cut short gives no data", "[LLPrimitive]")
    {
        object_data_t data = { 1, 2 };
        U8 byte = 7;
        LLDataPackerBinaryBuffer tree_dp(&byte, 0);
        CHECK(compressed_data(tree_dp, 0x2, data).empty());

        data = { 1, 2 };
        U8 pad[] = { 99, 0, 0, 0, 10, 0, 0, 0, 1, 2 };
        LLDataPackerBinaryBuffer pad_dp(pad, sizeof(pad));
        CHECK(compressed_data(pad_dp, 0x1, data).empty());
    }

    // A render material packs to no more than fourteen entries, and the count
    // before them says how many follow.
    TEST_CASE("LLRenderMaterialParams: pack writes as many entries as its count says", "[LLRenderMaterialParams]")
    {
        LLRenderMaterialParams params;
        for (U8 te = 0; te < 20; ++te)
        {
            LLUUID id;
            id.mData[0] = te + 1;
            id.mData[15] = 0xC3;
            params.setMaterial(te, id);
        }

        std::vector<U8> data(1024);
        LLDataPackerBinaryBuffer writer(data.data(), (S32)data.size());
        REQUIRE(params.pack(writer));
        data.resize(writer.getCurrentSize());
        REQUIRE_FALSE(data.empty());
        CHECK(data[0] == 14);
        CHECK(data.size() == 1 + 17 * (size_t)14);
        CHECK(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, data));

        LLDataPackerBinaryBuffer reader(data.data(), (S32)data.size());
        LLRenderMaterialParams read;
        CHECK(read.unpack(reader));
        CHECK_FALSE(reader.hasNext());
        for (U8 te = 0; te < 20; ++te)
        {
            CAPTURE((int)te);
            CHECK(read.getMaterial(te) == (te < 14 ? params.getMaterial(te) : LLUUID::null));
        }
    }

    // A simulator clears an object's render materials with a count of 0
    // and nothing after it.
    TEST_CASE("LLRenderMaterialParams: an empty render material is valid and unpacks to no entries", "[LLRenderMaterialParams]")
    {
        std::vector<U8> data = render_material_data(0);
        REQUIRE(data == std::vector<U8>{ 0 });
        CHECK(is_valid(LLNetworkData::PARAMS_RENDER_MATERIAL, data));

        LLRenderMaterialParams params;
        params.setMaterial(2, LLUUID::generateNewID());
        LLDataPackerBinaryBuffer dp(data.data(), (S32)data.size());
        CHECK(params.unpack(dp));
        CHECK(params.isEmpty());
        CHECK_FALSE(dp.hasNext());
    }

    TEST_CASE("LLRenderMaterialParams: a render material of more than sixty entries unpacks every entry", "[LLRenderMaterialParams]")
    {
        std::vector<U8> data = render_material_data(200);
        LLDataPackerBinaryBuffer dp(data.data(), (S32)data.size());
        LLRenderMaterialParams params;
        CHECK(params.unpack(dp));
        for (U8 i = 0; i < 200; ++i)
        {
            CAPTURE((int)i);
            LLUUID expected;
            memcpy(expected.mData, &data[1 + 17 * (size_t)i + 1], 16);
            CHECK(params.getMaterial(i) == expected);
        }
    }
}

#include "llmessagesystem_stub.cpp"
