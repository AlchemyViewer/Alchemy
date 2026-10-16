/**
 * @file   llfetchedgltfmaterial.cpp
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llfetchedgltfmaterial.h"

#include "llviewertexturelist.h"
#include "llavatarappearancedefines.h"
#include "llviewerobject.h"
#include "llselectmgr.h"
#include "llshadermgr.h"
#include "pipeline.h"

//static
LLFetchedGLTFMaterial LLFetchedGLTFMaterial::sDefault;

LLFetchedGLTFMaterial::LLFetchedGLTFMaterial()
    : LLGLTFMaterial()
    , mExpectedFlusTime(0.f)
{

}

LLFetchedGLTFMaterial::~LLFetchedGLTFMaterial()
{

}

LLFetchedGLTFMaterial& LLFetchedGLTFMaterial::operator=(const LLFetchedGLTFMaterial& rhs)
{
    LLGLTFMaterial::operator =(rhs);

    mBaseColorTexture = rhs.mBaseColorTexture;
    mNormalTexture = rhs.mNormalTexture;
    mMetallicRoughnessTexture = rhs.mMetallicRoughnessTexture;
    mEmissiveTexture = rhs.mEmissiveTexture;

    return *this;
}

// The sampler every glTF map is read through.
//
// Named here, by the material, rather than inherited from whatever mode each image happened to
// be carrying. That is the direction sampling ownership is moving: a texture is data, and how a
// pass reads it belongs to the pass. It matters more for glTF than elsewhere -- the format
// specifies sampler state per texture REFERENCE, so two materials may legitimately want one
// image read two ways, which an image-owned mode cannot represent at all.
//
// Anisotropic + wrap with the resource's mips, which is exactly what these textures resolved to
// before through LLImageGL's defaults -- so this names existing behaviour rather than changing
// it. SL's material subset carries no sampler fields yet; when it does, this constant becomes a
// lookup into the material.
// glTF splits its maps into colour and data, and so does the sampler.
//
// Base colour and emissive hold sRGB-encoded colour; the hardware decodes them, which is
// strictly better than the shader doing it because GL converts each texel BEFORE filtering.
// Converting afterwards linearises an average taken in sRGB space, and those are not the
// same -- srgb_to_linear(lerp(a,b,t)) != lerp(srgb_to_linear(a), srgb_to_linear(b), t) --
// which is what made minified albedo read too dark. The matching srgb_to_linear() calls in
// the PBR shaders are gone; keeping both would convert twice.
//
// Normal and ORM are measurements, not colour. They keep the default, which decodes nothing.
static constexpr ALSampler GLTF_COLOR_SAMPLER = ALSamplers::AnisoWrapSRGB;
static constexpr ALSampler GLTF_DATA_SAMPLER  = ALSamplers::AnisoWrap;

void LLFetchedGLTFMaterial::bind(LLViewerTexture* media_tex)
{
    // glTF 2.0 Specification 3.9.4. Alpha Coverage
    // mAlphaCutoff is only valid for LLGLTFMaterial::ALPHA_MODE_MASK
    F32 min_alpha = -1.0;

    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

    // override emissive and base color textures with media tex if present
    LLViewerTexture* baseColorTex = media_tex ? media_tex : mBaseColorTexture;
    LLViewerTexture* emissiveTex = media_tex ? media_tex : mEmissiveTexture;

    // sImpostorRender is checked alongside sShadowRender throughout this function because
    // generateImpostor sets BOTH, and only one of them means "depth-only". An impostor bake
    // runs the full G-buffer pools into a target with real ORM and normal attachments, which
    // impostorF then replays into the scene G-buffer, so everything a shadow pass is right
    // to skip is exactly what the bake needs.
    const bool depth_only_pass = LLPipeline::sShadowRender && !LLPipeline::sImpostorRender;

    if (!depth_only_pass || (mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK))
    {
        if (mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK)
        {
            min_alpha = mAlphaCutoff;
        }
        // setMinimumAlpha, not uniform1f: it owns the per-program value cache, and a run of
        // non-masked materials (all writing -1) collapses to a float compare per bind.
        shader->setMinimumAlpha(min_alpha);
    }

    if (baseColorTex != nullptr)
    {
        shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, baseColorTex, GLTF_COLOR_SAMPLER);
    }
    else
    {
        shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, LLViewerFetchedTexture::sWhiteImagep, GLTF_COLOR_SAMPLER);
    }

    F32 base_color_packed[8];
    mTextureTransform[GLTF_TEXTURE_INFO_BASE_COLOR].getPacked(base_color_packed);
    shader->uniform4fv(LLShaderMgr::TEXTURE_BASE_COLOR_TRANSFORM, 2, (F32*)base_color_packed);

    if (!depth_only_pass)
    {
        if (mNormalTexture.notNull() && mNormalTexture->getDiscardLevel() <= 4)
        {
            shader->bindTexture(LLShaderMgr::BUMP_MAP, mNormalTexture, GLTF_DATA_SAMPLER);
        }
        else
        {
            shader->bindTexture(LLShaderMgr::BUMP_MAP, LLViewerFetchedTexture::sFlatNormalImagep, GLTF_DATA_SAMPLER);
        }

        if (mMetallicRoughnessTexture.notNull())
        {
            shader->bindTexture(LLShaderMgr::SPECULAR_MAP, mMetallicRoughnessTexture, GLTF_DATA_SAMPLER); // PBR linear packed Occlusion, Roughness, Metal.
        }
        else
        {
            shader->bindTexture(LLShaderMgr::SPECULAR_MAP, LLViewerFetchedTexture::sWhiteImagep, GLTF_DATA_SAMPLER);
        }

        if (emissiveTex != nullptr)
        {
            shader->bindTexture(LLShaderMgr::EMISSIVE_MAP, emissiveTex, GLTF_COLOR_SAMPLER);  // PBR sRGB Emissive
        }
        else
        {
            shader->bindTexture(LLShaderMgr::EMISSIVE_MAP, LLViewerFetchedTexture::sWhiteImagep, GLTF_COLOR_SAMPLER);
        }

        // NOTE: base color factor is baked into vertex stream

        shader->uniform1f(LLShaderMgr::ROUGHNESS_FACTOR, mRoughnessFactor);
        shader->uniform1f(LLShaderMgr::METALLIC_FACTOR, mMetallicFactor);
        shader->uniform3fv(LLShaderMgr::EMISSIVE_COLOR, 1, mEmissiveColor.mV);

        F32 normal_packed[8];
        mTextureTransform[GLTF_TEXTURE_INFO_NORMAL].getPacked(normal_packed);
        shader->uniform4fv(LLShaderMgr::TEXTURE_NORMAL_TRANSFORM, 2, (F32*)normal_packed);

        F32 metallic_roughness_packed[8];
        mTextureTransform[GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS].getPacked(metallic_roughness_packed);
        shader->uniform4fv(LLShaderMgr::TEXTURE_METALLIC_ROUGHNESS_TRANSFORM, 2, (F32*)metallic_roughness_packed);

        F32 emissive_packed[8];
        mTextureTransform[GLTF_TEXTURE_INFO_EMISSIVE].getPacked(emissive_packed);
        shader->uniform4fv(LLShaderMgr::TEXTURE_EMISSIVE_TRANSFORM, 2, (F32*)emissive_packed);
    }
}

LLViewerFetchedTexture* fetch_texture(const LLUUID& id)
{
    LLViewerFetchedTexture* img = nullptr;
    if (id.notNull())
    {
        img = LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
        img->addTextureStats(64.f * 64.f, true);
    }
    return img;
};

bool LLFetchedGLTFMaterial::replaceLocalTexture(const LLUUID& tracking_id, const LLUUID& old_id, const LLUUID& new_id)
{
    bool res = false;
    if (mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR] == old_id)
    {
        mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR] = new_id;
        mBaseColorTexture = fetch_texture(new_id);
        res = true;
    }
    if (mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL] == old_id)
    {
        mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL] = new_id;
        mNormalTexture = fetch_texture(new_id);
        res = true;
    }
    if (mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS] == old_id)
    {
        mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS] = new_id;
        mMetallicRoughnessTexture = fetch_texture(new_id);
        res = true;
    }
    if (mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE] == old_id)
    {
        mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE] = new_id;
        mEmissiveTexture = fetch_texture(new_id);
        res = true;
    }

    for (int i = 0; i < GLTF_TEXTURE_INFO_COUNT; ++i)
    {
        if (mTextureId[i] == new_id)
        {
            res = true;
        }
    }

    if (res)
    {
        mTrackingIdToLocalTexture[tracking_id] = new_id;
    }
    else
    {
        mTrackingIdToLocalTexture.erase(tracking_id);
    }
    updateLocalTexDataDigest();

    return res;
}

void LLFetchedGLTFMaterial::addTextureEntry(LLTextureEntry* te)
{
    mTextureEntires.insert(te);
}

void LLFetchedGLTFMaterial::removeTextureEntry(LLTextureEntry* te)
{
    mTextureEntires.erase(te);
}

void LLFetchedGLTFMaterial::updateTextureTracking()
{
    for (local_tex_map_t::value_type &val : mTrackingIdToLocalTexture)
    {
        LLLocalBitmapMgr::getInstance()->associateGLTFMaterial(val.first, this);
    }
}

void LLFetchedGLTFMaterial::clearFetchedTextures()
{
    mBaseColorTexture = nullptr;
    mNormalTexture = nullptr;
    mMetallicRoughnessTexture = nullptr;
    mEmissiveTexture = nullptr;
}

void LLFetchedGLTFMaterial::materialBegin()
{
    llassert(!mFetching);
    mFetching = true;
}

void LLFetchedGLTFMaterial::onMaterialComplete(std::function<void()> material_complete)
{
    if (!material_complete) { return; }

    if (!mFetching)
    {
        material_complete();
        return;
    }

    materialCompleteCallbacks.push_back(material_complete);
}

void LLFetchedGLTFMaterial::materialComplete(bool success)
{
    llassert(mFetching);
    mFetching = false;
    mFetchSuccess = success;

    for (std::function<void()> material_complete : materialCompleteCallbacks)
    {
        material_complete();
    }
    materialCompleteCallbacks.clear();
    materialCompleteCallbacks.shrink_to_fit();
}
