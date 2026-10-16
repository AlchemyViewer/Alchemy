/**
 * @file llgltfmaterial_test.cpp
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

#include <set>

#include "../llgltfmaterial.h"

// Import & define single-header gltf import/export lib
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14  // default is C++ 11

// tinygltf by default loads image files using STB
#define STB_IMAGE_IMPLEMENTATION
// to use our own image loading:
// 1. replace this definition with TINYGLTF_NO_STB_IMAGE
// 2. provide image loader callback with TinyGLTF::SetImageLoader(LoadimageDataFunction LoadImageData, void *user_data)

// tinygltf saves image files using STB
#define STB_IMAGE_WRITE_IMPLEMENTATION
// similarly, can override with TINYGLTF_NO_STB_IMAGE_WRITE and TinyGLTF::SetImageWriter(fxn, data)

// Disable reading external images to prevent warnings and speed up the tests.
// We don't need this for the tests, but still need the filesystem
// implementation to be defined in order for llprimitive to link correctly.
#define TINYGLTF_NO_EXTERNAL_IMAGE 1

#include <tiny_gltf.h>

namespace
{
    // A positive 32-bit float with a long string representation
    constexpr F32 test_fraction = 1.09045365e-32f;
    // A larger positive 32-bit float for values that get zeroed if below a threshold
    constexpr F32 test_fraction_big = 0.109045f;

    void apply_test_material_texture_ids(LLGLTFMaterial& material)
    {
        material.setBaseColorId(LLUUID::generateNewID());
        material.setNormalId(LLUUID::generateNewID());
        material.setOcclusionRoughnessMetallicId(LLUUID::generateNewID());
        material.setEmissiveId(LLUUID::generateNewID());
    }

    void apply_test_material_texture_transforms(LLGLTFMaterial& material)
    {
        LLGLTFMaterial::TextureTransform test_transform;
        test_transform.mOffset.mV[VX] = test_fraction;
        test_transform.mOffset.mV[VY] = test_fraction;
        test_transform.mScale.mV[VX] = test_fraction;
        test_transform.mScale.mV[VY] = test_fraction;
        test_transform.mRotation = test_fraction;
        for (LLGLTFMaterial::TextureInfo i = LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; i = LLGLTFMaterial::TextureInfo((U32)i + 1))
        {
            material.setTextureOffset(i, test_transform.mOffset);
            material.setTextureScale(i, test_transform.mScale);
            material.setTextureRotation(i, test_transform.mRotation);
        }
    }

    void apply_test_material_factors(LLGLTFMaterial& material)
    {
        material.setBaseColorFactor(LLColor4(test_fraction_big, test_fraction_big, test_fraction_big, test_fraction_big));
        material.setEmissiveColorFactor(LLColor3(test_fraction_big, test_fraction_big, test_fraction_big));
        material.setMetallicFactor(test_fraction);
        material.setRoughnessFactor(test_fraction);
    }

    LLGLTFMaterial create_test_material()
    {
        LLGLTFMaterial material;

        apply_test_material_texture_ids(material);

        apply_test_material_texture_transforms(material);

        apply_test_material_factors(material);

        material.setAlphaCutoff(test_fraction);
        // Because this is the default value, it should append to the extras field to mark it as an override
        material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_OPAQUE, true);
        // Because this is the default value, it should append to the extras field to mark it as an override
        material.setDoubleSided(false, true);

        return material;
    }

    void require_json_round_trip(const std::string& what, const LLGLTFMaterial& material_in)
    {
        INFO(what);
        const std::string json_in = material_in.asJSON();
        LLGLTFMaterial material_out;
        std::string warn_msg;
        std::string error_msg;
        bool serialize_success = material_out.fromJSON(json_in, warn_msg, error_msg);
        REQUIRE(warn_msg == "");
        REQUIRE(error_msg == "");
        REQUIRE(serialize_success);
        REQUIRE(material_in == material_out);
        const std::string json_out = material_out.asJSON();
        REQUIRE(json_in == json_out);
    }

    void require_trimmed(const std::string& material_json, const std::string& must_not_contain)
    {
        CAPTURE(must_not_contain);
        REQUIRE(material_json.find(must_not_contain) == std::string::npos);
    }

    // Test that GLTF material fields have not changed since these tests were
    // written. The previous sizeof(LLGLTFMaterial) tripwire was removed when
    // the canonical hash packing was added — total class size now varies with
    // the hash snapshot/cache members (which aren't part of the hashed
    // payload), so it stopped being a useful regression check. The case
    // "getHash covers every field but the local texture tracking map", at the
    // end of this file, exercises the actual hash invariant directly.
    TEST_CASE("llgltfmaterial: there are four texture infos", "[llgltfmaterial]")
    {
        CHECK((U32)LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT == 4);
    }

    // Test that occlusion and metallicRoughness are the same (They are different for asset validation. See lluploadmaterial.cpp)
    TEST_CASE("llgltfmaterial: occlusion and metallicRoughness share a texture info", "[llgltfmaterial]")
    {
        CHECK(LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS == LLGLTFMaterial::GLTF_TEXTURE_INFO_OCCLUSION);
    }

    // Ensure double sided and alpha mode overrides serialize as expected
    TEST_CASE("llgltfmaterial: double sided and alpha mode overrides set their flag and serialize", "[llgltfmaterial]")
    {
        const bool doubleSideds[] { false, true };
        const LLGLTFMaterial::AlphaMode alphaModes[] { LLGLTFMaterial::ALPHA_MODE_OPAQUE, LLGLTFMaterial::ALPHA_MODE_BLEND, LLGLTFMaterial::ALPHA_MODE_MASK };
        const bool forOverrides[] { false, true };

        for (bool doubleSided : doubleSideds)
        {
            for (bool forOverride : forOverrides)
            {
                CAPTURE(doubleSided, forOverride);
                LLGLTFMaterial material;
                material.setDoubleSided(doubleSided, forOverride);
                const bool overrideBit = (doubleSided == false) && forOverride;
                REQUIRE(material.mOverrideDoubleSided == overrideBit);
                require_json_round_trip("double sided = " + std::to_string(doubleSided), material);
            }
        }

        for (LLGLTFMaterial::AlphaMode alphaMode : alphaModes)
        {
            for (bool forOverride : forOverrides)
            {
                CAPTURE(alphaMode, forOverride);
                LLGLTFMaterial material;
                material.setAlphaMode(alphaMode, forOverride);
                const bool overrideBit = (alphaMode == LLGLTFMaterial::ALPHA_MODE_OPAQUE) && forOverride;
                REQUIRE(material.mOverrideAlphaMode == overrideBit);
                require_json_round_trip("alpha mode = " + std::to_string(alphaMode), material);
            }
        }
    }

    // Test that a GLTF material's transform components serialize as expected
    TEST_CASE("llgltfmaterial: a texture transform survives a JSON round trip", "[llgltfmaterial]")
    {
        LLGLTFMaterial material;
        LLGLTFMaterial::TextureTransform& transform = material.mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR];
        transform.mOffset[VX] = 1.f;
        transform.mOffset[VY] = 2.f;
        transform.mScale[VX] = 0.05f;
        transform.mScale[VY] = 100.f;
        transform.mRotation = 1.571f;
        require_json_round_trip("material with transform", material);
    }

    // Test that a GLTF material avoids serializing a material unnecessarily
    TEST_CASE("llgltfmaterial: asJSON leaves out default properties and unset textures", "[llgltfmaterial]")
    {
        {
            const LLGLTFMaterial material;
            const std::string material_json = material.asJSON();
            require_trimmed(material_json, "pbrMetallicRoughness");
            require_trimmed(material_json, "normalTexture");
            require_trimmed(material_json, "emissiveTexture");
            require_trimmed(material_json, "occlusionTexture");
        }

        {
            LLGLTFMaterial metallic_factor_material;
            metallic_factor_material.setMetallicFactor(0.5);
            const std::string metallic_factor_material_json = metallic_factor_material.asJSON();
            require_trimmed(metallic_factor_material_json, "baseColorTexture");
            require_trimmed(metallic_factor_material_json, "metallicRoughnessTexture");
        }
    }

    // Test that a GLTF material preserves values on serialization
    TEST_CASE("llgltfmaterial: full and partial materials survive a JSON round trip", "[llgltfmaterial]")
    {
        {
            const LLGLTFMaterial full_material = create_test_material();
            require_json_round_trip("full material", full_material);
        }

        {
            LLGLTFMaterial texture_ids_only_material;
            apply_test_material_texture_ids(texture_ids_only_material);
            require_json_round_trip("material with texture IDs only", texture_ids_only_material);
        }

        {
            LLGLTFMaterial texture_transforms_only_material;
            apply_test_material_texture_transforms(texture_transforms_only_material);
            require_json_round_trip("material with texture transforms only", texture_transforms_only_material);
        }

        {
            LLGLTFMaterial factors_only_material;
            apply_test_material_factors(factors_only_material);
            require_json_round_trip("material with scaling/tint factors only", factors_only_material);
        }
    }

    TEST_CASE("llgltfmaterial: sDefault is a no-op override", "[llgltfmaterial]")
    {
        const LLGLTFMaterial material_asset = create_test_material();
        LLGLTFMaterial render_material = material_asset;
        render_material.applyOverride(LLGLTFMaterial::sDefault);
        CHECK(material_asset == render_material);
    }

    // Test application of transform overrides
    TEST_CASE("llgltfmaterial: applyOverride applies texture transform overrides", "[llgltfmaterial]")
    {
        LLGLTFMaterial override_material;
        apply_test_material_texture_transforms(override_material);
        LLGLTFMaterial render_material;
        render_material.applyOverride(override_material);
        CHECK(render_material == override_material);
    }

    // Test application of flag-based overrides
    TEST_CASE("llgltfmaterial: applyOverride applies alpha mode and double sided overrides", "[llgltfmaterial]")
    {
        {
            LLGLTFMaterial override_material;
            override_material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_BLEND, true);
            override_material.setDoubleSided(true, true);

            LLGLTFMaterial render_material;

            render_material.applyOverride(override_material);

            UNSCOPED_INFO("non-default overrides applied over defaults");
            CHECK(render_material == override_material);
        }
        {
            // The default values, flagged as overrides, set a non-default
            // material back to the defaults
            LLGLTFMaterial override_material;
            override_material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_OPAQUE, true);
            override_material.setDoubleSided(false, true);

            LLGLTFMaterial render_material;
            render_material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_BLEND, false);
            render_material.setDoubleSided(true, false);

            render_material.applyOverride(override_material);
            // applyOverride takes the values, not the override flags
            override_material.mOverrideDoubleSided = false;
            override_material.mOverrideAlphaMode = false;

            UNSCOPED_INFO("flagged default overrides applied over non-defaults");
            CHECK(render_material == override_material);
        }
        {
            // Without the flags, the same default values are no override at
            // all and leave a non-default material alone
            LLGLTFMaterial override_material;
            override_material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_OPAQUE, false);
            override_material.setDoubleSided(false, false);

            LLGLTFMaterial render_material;
            render_material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_BLEND, false);
            render_material.setDoubleSided(true, false);
            const LLGLTFMaterial before_override = render_material;

            render_material.applyOverride(override_material);

            UNSCOPED_INFO("unflagged default values applied over non-defaults");
            CHECK(render_material == before_override);
        }
    }

    // Test application of texture overrides
    TEST_CASE("llgltfmaterial: applyOverride replaces every texture ID with the override", "[llgltfmaterial]")
    {
        const U32 texture_count = 2;
        const LLUUID override_textures[texture_count] = { LLUUID::null, LLUUID::generateNewID() };
        const LLUUID asset_textures[texture_count] = { LLUUID::generateNewID(), LLUUID::null };
        for (U32 i = 0; i < texture_count; ++i)
        {
            LLGLTFMaterial override_material;
            const LLUUID& override_texture = override_textures[i];
            for (LLGLTFMaterial::TextureInfo j = LLGLTFMaterial::TextureInfo(0); j  < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; j = LLGLTFMaterial::TextureInfo(U32(j) + 1))
            {
                override_material.setTextureId(j, override_texture, true);
            }

            LLGLTFMaterial render_material;
            const LLUUID& asset_texture = asset_textures[i];
            for (LLGLTFMaterial::TextureInfo j = LLGLTFMaterial::TextureInfo(0); j  < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; j = LLGLTFMaterial::TextureInfo(U32(j) + 1))
            {
                render_material.setTextureId(j, asset_texture, false);
            }

            render_material.applyOverride(override_material);

            for (LLGLTFMaterial::TextureInfo j = LLGLTFMaterial::TextureInfo(0); j  < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; j = LLGLTFMaterial::TextureInfo(U32(j) + 1))
            {
                const LLUUID& render_texture = render_material.mTextureId[j];
                CAPTURE(override_texture, asset_texture);
                REQUIRE(render_texture == override_texture);
            }
        }
    }

    // Test non-persistence of default value flags in overrides
    TEST_CASE("llgltfmaterial: setting a non-default value clears the override flag", "[llgltfmaterial]")
    {
        const S32 non_default_alpha_modes[] = { LLGLTFMaterial::ALPHA_MODE_BLEND, LLGLTFMaterial::ALPHA_MODE_MASK };
        for (S32 non_default_alpha_mode : non_default_alpha_modes)
        {
            CAPTURE(non_default_alpha_mode);
            LLGLTFMaterial material;
            // Set default alpha mode
            material.setAlphaMode(LLGLTFMaterial::ALPHA_MODE_OPAQUE, true);
            REQUIRE(material.mOverrideAlphaMode);
            // Set non-default alpha mode
            material.setAlphaMode(non_default_alpha_mode, true);
            REQUIRE_FALSE(material.mOverrideAlphaMode);
        }

        {
            // Set default double sided
            LLGLTFMaterial material;
            material.setDoubleSided(false, true);
            CHECK(material.mOverrideDoubleSided);
            // Set non-default double sided
            material.setDoubleSided(true, true);
            CHECK_FALSE(material.mOverrideDoubleSided);
        }
    }

    // the field must lie inside the material and the new value must differ
    template<typename T>
    void require_perturbable_field(LLGLTFMaterial& material, T& material_field, const T new_value)
    {
        {
            INFO("the field is part of the test material object");
            REQUIRE(size_t(&material_field) >= size_t(&material));
            REQUIRE((size_t(&material_field) + sizeof(material_field)) <= (size_t(&material) + sizeof(material)));
        }
        REQUIRE(material_field != new_value);
    }

    // perturbing the field to the new value does not change the hash
    template<typename T>
    void require_hash_ignores(LLGLTFMaterial& material, T& material_field, const T new_value, const std::string& field_name)
    {
        CAPTURE(field_name);
        require_perturbable_field(material, material_field, new_value);

        const LLGLTFMaterial old_material = material;
        material_field = new_value;
        // If this test fails, consult LLGLTFMaterial::getHash, and optionally consult http://www.catb.org/esr/structure-packing/ for guidance on optimal memory packing (effectiveness is platform-dependent)
        REQUIRE(material.getHash() == old_material.getHash());
    }

    // perturbing the field to the new value changes the hash
    template<typename T>
    void require_hash_covers(LLGLTFMaterial& material, T& material_field, const T new_value, const std::string& field_name)
    {
        CAPTURE(field_name);
        require_perturbable_field(material, material_field, new_value);

        const LLGLTFMaterial old_material = material;
        material_field = new_value;
        // If this test fails, consult LLGLTFMaterial::getHash, and optionally consult http://www.catb.org/esr/structure-packing/ for guidance on optimal memory packing (effectiveness is platform-dependent)
        REQUIRE(material.getHash() != old_material.getHash());
    }

#define HASH_IGNORES(HASH_MAT, SOURCE_MAT, FIELD) require_hash_ignores(HASH_MAT, HASH_MAT.FIELD, SOURCE_MAT.FIELD, #FIELD)
#define HASH_COVERS(HASH_MAT, SOURCE_MAT, FIELD) require_hash_covers(HASH_MAT, HASH_MAT.FIELD, SOURCE_MAT.FIELD, #FIELD)

    // Test LLGLTFMaterial::getHash. The hash is now built from a canonical
    // packing of the meaningful members, so the previous structure-layout
    // fragility is gone.
    TEST_CASE("llgltfmaterial: getHash covers every field but the local texture tracking map", "[llgltfmaterial]")
    {
        // *NOTE: Due to direct manipulation of the fields of materials
        // throughout this test, the resulting modified materials may not be
        // compliant or properly serializable.

        // Ensure all fields of source_mat are set to values that differ from
        // LLGLTFMaterial::sDefault, even if that would result in an invalid
        // material object.
        LLGLTFMaterial source_mat = create_test_material();
        source_mat.mTrackingIdToLocalTexture[LLUUID::generateNewID()] = LLUUID::generateNewID();
        source_mat.mLocalTexDataDigest = 1;
        source_mat.mAlphaMode = LLGLTFMaterial::ALPHA_MODE_MASK;
        source_mat.mDoubleSided = true;

        LLGLTFMaterial hash_mat;

        HASH_IGNORES(hash_mat, source_mat, mTrackingIdToLocalTexture);
        HASH_COVERS(hash_mat, source_mat, mLocalTexDataDigest);

        HASH_COVERS(hash_mat, source_mat, mTextureId);
        HASH_COVERS(hash_mat, source_mat, mTextureTransform);
        HASH_COVERS(hash_mat, source_mat, mBaseColor);
        HASH_COVERS(hash_mat, source_mat, mEmissiveColor);
        HASH_COVERS(hash_mat, source_mat, mMetallicFactor);
        HASH_COVERS(hash_mat, source_mat, mRoughnessFactor);
        HASH_COVERS(hash_mat, source_mat, mAlphaCutoff);
        HASH_COVERS(hash_mat, source_mat, mAlphaMode);
        HASH_COVERS(hash_mat, source_mat, mDoubleSided);
        HASH_COVERS(hash_mat, source_mat, mOverrideDoubleSided);
        HASH_COVERS(hash_mat, source_mat, mOverrideAlphaMode);
    }

namespace
{
    // Notes the tracking ids updateTextureTracking() finds, where
    // LLFetchedGLTFMaterial associates each with its local bitmap
    struct tracking_material : public LLGLTFMaterial
    {
        void updateTextureTracking() override
        {
            mTrackedIds.clear();
            for (const auto& [tracking_id, local_texture] : mTrackingIdToLocalTexture)
            {
                mTrackedIds.insert(tracking_id);
            }
        }

        std::set<LLUUID> mTrackedIds;
    };
}

    TEST_CASE("llgltfmaterial: applying an override tracks its local texture on the render material, beside the material's own", "[llgltfmaterial]")
    {
        const LLUUID base_tracking_id = LLUUID::generateNewID();
        const LLUUID base_local_texture = LLUUID::generateNewID();
        const LLUUID override_tracking_id = LLUUID::generateNewID();
        const LLUUID override_local_texture = LLUUID::generateNewID();

        LLGLTFMaterial override_material;
        override_material.setBaseColorId(override_local_texture, true);
        override_material.addLocalTextureTracking(override_tracking_id, override_local_texture);

        tracking_material render_material;
        apply_test_material_texture_ids(render_material);
        render_material.setNormalId(base_local_texture);
        render_material.addLocalTextureTracking(base_tracking_id, base_local_texture);

        render_material.applyOverride(override_material);

        CHECK(render_material.mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR] == override_local_texture);
        // Each tracked by its world id.
        const auto override_tracked = render_material.mTrackingIdToLocalTexture.find(override_tracking_id);
        REQUIRE(override_tracked != render_material.mTrackingIdToLocalTexture.end());
        CHECK(override_tracked->second == override_local_texture);
        const auto base_tracked = render_material.mTrackingIdToLocalTexture.find(base_tracking_id);
        REQUIRE(base_tracked != render_material.mTrackingIdToLocalTexture.end());
        CHECK(base_tracked->second == base_local_texture);
        CHECK(render_material.mTrackingIdToLocalTexture.size() == size_t(2));

        // updateTextureTracking finds both.
        CHECK(render_material.mTrackedIds.count(override_tracking_id) == 1);
        CHECK(render_material.mTrackedIds.count(base_tracking_id) == 1);

        // The local texture digest covers the override's.
        LLGLTFMaterial expected_material;
        expected_material.addLocalTextureTracking(base_tracking_id, base_local_texture);
        expected_material.addLocalTextureTracking(override_tracking_id, override_local_texture);
        CHECK(render_material.mLocalTexDataDigest == expected_material.mLocalTexDataDigest);
    }

    // Both track the same local texture: the override's entry wins, as its
    // texture ids do
    TEST_CASE("llgltfmaterial: applying an override that tracks the material's local texture takes the override's world id", "[llgltfmaterial]")
    {
        const LLUUID tracking_id = LLUUID::generateNewID();
        const LLUUID base_local_texture = LLUUID::generateNewID();
        const LLUUID override_local_texture = LLUUID::generateNewID();

        LLGLTFMaterial override_material;
        override_material.setBaseColorId(override_local_texture, true);
        override_material.addLocalTextureTracking(tracking_id, override_local_texture);

        LLGLTFMaterial render_material;
        render_material.setBaseColorId(base_local_texture);
        render_material.addLocalTextureTracking(tracking_id, base_local_texture);

        render_material.applyOverride(override_material);

        const auto tracked = render_material.mTrackingIdToLocalTexture.find(tracking_id);
        REQUIRE(tracked != render_material.mTrackingIdToLocalTexture.end());
        CHECK(tracked->second == override_local_texture);
    }
}
