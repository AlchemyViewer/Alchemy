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
}

#include "llmessagesystem_stub.cpp"
