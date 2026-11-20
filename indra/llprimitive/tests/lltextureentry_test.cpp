/**
 * @file lltextureentry_test.cpp
 * @brief What a face does with the glTF override it holds.
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

#include "../lltextureentry.h"
#include "../llgltfmaterial.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("LLTextureEntry: setBaseMaterial changes a copy of the override the face holds", "[LLTextureEntry]")
    {
        LLTextureEntry te;
        LLPointer<LLGLTFMaterial> base = new LLGLTFMaterial();
        te.setGLTFMaterial(base);

        // An override the region's record holds too, with a transform setBaseMaterial keeps and a factor it clears.
        LLPointer<LLGLTFMaterial> shared = new LLGLTFMaterial();
        shared->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].mScale.set(2.f, 2.f);
        shared->mRoughnessFactor = 0.25f;
        const LLGLTFMaterial before = *shared;
        te.setGLTFMaterialOverride(shared);

        CHECK(te.setBaseMaterial() == TEM_CHANGE_TEXTURE);
        UNSCOPED_INFO("the shared override is as it was");
        CHECK(*shared == before);
        const LLGLTFMaterial* own = te.getGLTFMaterialOverride();
        REQUIRE(own);
        UNSCOPED_INFO("the face holds a copy of its own");
        CHECK(own != shared.get());
        CHECK(own->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].mScale == LLVector2(2.f, 2.f));
        CHECK(own->mRoughnessFactor == LLGLTFMaterial::sDefault.mRoughnessFactor);

        UNSCOPED_INFO("an override that clears to nothing leaves the face with none and the shared one as it was");
        LLPointer<LLGLTFMaterial> factor_only = new LLGLTFMaterial();
        factor_only->mMetallicFactor = 0.5f;
        const LLGLTFMaterial factor_before = *factor_only;
        te.setGLTFMaterialOverride(factor_only);
        CHECK(te.setBaseMaterial() == TEM_CHANGE_TEXTURE);
        CHECK(te.getGLTFMaterialOverride() == nullptr);
        CHECK(*factor_only == factor_before);

        UNSCOPED_INFO("a face with no override has nothing to change");
        CHECK(te.setBaseMaterial() == TEM_CHANGE_NONE);
    }
}
