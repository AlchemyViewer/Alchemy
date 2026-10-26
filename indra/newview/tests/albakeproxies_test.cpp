/**
 * @file albakeproxies_test.cpp
 * @brief Unit tests for the bakes a face names through bake proxies
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../albakeproxies.h"

#include "indra_constants.h"
#include "llgltfmaterial.h"
#include "lltextureentry.h"

#include <initializer_list>
#include <utility>

namespace
{
    using namespace ALBakeProxies;
    using namespace LLAvatarAppearanceDefines;

    // The set of the given bakes, as a number Catch prints
    unsigned long bakes(std::initializer_list<EBakedTextureIndex> indices)
    {
        bake_set_t set;
        for (EBakedTextureIndex index : indices)
        {
            set.set(index);
        }
        return set.to_ulong();
    }

    // A face and the PBR material it is drawn with
    struct face_data
    {
        LLPointer<LLGLTFMaterial> mMaterial = new LLGLTFMaterial();
        LLTextureEntry mTE;

        face_data() : mTE(IMG_DEFAULT) { mTE.setGLTFMaterial(mMaterial); }

        LLUUID& slot(LLGLTFMaterial::TextureInfo info) { return mMaterial->mTextureId[info]; }
    };

    TEST_CASE("ALBakeProxies: every bake proxy names its own bake", "[ALBakeProxies]")
    {
        const std::pair<const LLUUID*, EBakedTextureIndex> proxies[] = {
            { &IMG_USE_BAKED_HEAD, BAKED_HEAD },         { &IMG_USE_BAKED_UPPER, BAKED_UPPER },
            { &IMG_USE_BAKED_LOWER, BAKED_LOWER },       { &IMG_USE_BAKED_EYES, BAKED_EYES },
            { &IMG_USE_BAKED_SKIRT, BAKED_SKIRT },       { &IMG_USE_BAKED_HAIR, BAKED_HAIR },
            { &IMG_USE_BAKED_LEFTARM, BAKED_LEFT_ARM },  { &IMG_USE_BAKED_LEFTLEG, BAKED_LEFT_LEG },
            { &IMG_USE_BAKED_AUX1, BAKED_AUX1 },         { &IMG_USE_BAKED_AUX2, BAKED_AUX2 },
            { &IMG_USE_BAKED_AUX3, BAKED_AUX3 },
        };
        for (const auto& [id, index] : proxies)
        {
            LLTextureEntry te(*id);
            CHECK(drawnBy(te, false).to_ulong() == bakes({ index }));
        }
    }

    TEST_CASE("ALBakeProxies: a face with no proxy draws no bake", "[ALBakeProxies]")
    {
        LLTextureEntry te(IMG_DEFAULT);
        CHECK(drawnBy(te, false).none());
        CHECK(drawnBy(nullptr).none());

        LLPointer<LLGLTFMaterial> material = new LLGLTFMaterial();
        CHECK_FALSE(anyNamedBy(*material));
    }

    TEST_CASE_METHOD(face_data, "ALBakeProxies: a PBR material draws the bake its base colour names", "[ALBakeProxies]")
    {
        slot(LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR) = IMG_USE_BAKED_UPPER;
        CHECK(drawnBy(mMaterial).to_ulong() == bakes({ BAKED_UPPER }));
        CHECK(drawnBy(mTE, false).to_ulong() == bakes({ BAKED_UPPER }));

        // the image's bake and the material's together
        mTE.setID(IMG_USE_BAKED_HEAD);
        CHECK(drawnBy(mTE, false).to_ulong() == bakes({ BAKED_HEAD, BAKED_UPPER }));
    }

    TEST_CASE_METHOD(face_data, "ALBakeProxies: a bake in a material's other slots draws no body", "[ALBakeProxies]")
    {
        slot(LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL) = IMG_USE_BAKED_UPPER;
        slot(LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS) = IMG_USE_BAKED_HEAD;
        slot(LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE) = IMG_USE_BAKED_HAIR;

        CHECK(drawnBy(mMaterial).none());
        CHECK(drawnBy(mTE, false).none());
        // but each is resolved for the face's own avatar
        CHECK(anyNamedBy(*mMaterial));
    }

    TEST_CASE_METHOD(face_data, "ALBakeProxies: a HUD's face counts its image and not its material", "[ALBakeProxies]")
    {
        mTE.setID(IMG_USE_BAKED_HEAD);
        slot(LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR) = IMG_USE_BAKED_UPPER;

        CHECK(drawnBy(mTE, true).to_ulong() == bakes({ BAKED_HEAD }));
        CHECK(drawnBy(mTE, false).to_ulong() == bakes({ BAKED_HEAD, BAKED_UPPER }));
    }
}
